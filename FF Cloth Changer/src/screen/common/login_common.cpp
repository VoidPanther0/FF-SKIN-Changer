#include "login_common.h"
#include "imgui_internal.h"
#include "../../Resources/resource.h"

#include <wincodec.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace login_ui {

namespace {

using namespace ui;

constexpr int kLogoSize = 120;
constexpr double kLogoRetrySeconds = 3.0;

ID3D11ShaderResourceView* logo_texture = nullptr;
double logo_next_try = 0.0;

std::string notice_text;
double notice_time = -1.0;

// Decodes the embedded PNG with WIC, scales it to kLogoSize and uploads it as a
// Direct3D 11 texture. Every WIC object is released on every path.
bool load_logo()
{
	if (!g_pd3dDevice)
		return false;

	HMODULE module = GetModuleHandleW(nullptr);
	HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_LOGO), RT_RCDATA);
	if (!resource)
		return false;

	HGLOBAL handle = LoadResource(module, resource);
	void* bytes = handle ? LockResource(handle) : nullptr;
	const DWORD size = SizeofResource(module, resource);
	if (!bytes || size == 0)
		return false;

	// S_FALSE means COM was already started on this thread; both succeed and
	// both need a matching CoUninitialize. RPC_E_CHANGED_MODE needs none.
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool com_ready = SUCCEEDED(com);

	IWICImagingFactory* factory = nullptr;
	IWICStream* stream = nullptr;
	IWICBitmapDecoder* decoder = nullptr;
	IWICBitmapFrameDecode* frame = nullptr;
	IWICFormatConverter* converter = nullptr;
	IWICBitmapScaler* scaler = nullptr;
	IWICFormatConverter* output = nullptr;

	const UINT side = static_cast<UINT>(kLogoSize);
	std::vector<unsigned char> pixels(static_cast<size_t>(side) * side * 4);

	const bool decoded =
		SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		SUCCEEDED(factory->CreateStream(&stream)) &&
		SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(bytes), size)) &&
		SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
		SUCCEEDED(decoder->GetFrame(0, &frame)) &&
		SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
		SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone,
			nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
		SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
		SUCCEEDED(scaler->Initialize(converter, side, side, WICBitmapInterpolationModeFant)) &&
		SUCCEEDED(factory->CreateFormatConverter(&output)) &&
		SUCCEEDED(output->Initialize(scaler, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone,
			nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
		SUCCEEDED(output->CopyPixels(nullptr, side * 4, static_cast<UINT>(pixels.size()), pixels.data()));

	if (output) output->Release();
	if (scaler) scaler->Release();
	if (converter) converter->Release();
	if (frame) frame->Release();
	if (decoder) decoder->Release();
	if (stream) stream->Release();
	if (factory) factory->Release();
	if (com_ready)
		CoUninitialize();

	if (!decoded)
		return false;

	// Scaled premultiplied so edges stay clean; ImGui blends straight alpha.
	for (size_t i = 0; i < pixels.size(); i += 4)
	{
		const unsigned alpha = pixels[i + 3];
		if (alpha == 0 || alpha == 255)
			continue;
		for (size_t c = 0; c < 3; ++c)
		{
			const unsigned value = pixels[i + c] * 255u / alpha;
			pixels[i + c] = static_cast<unsigned char>(value > 255u ? 255u : value);
		}
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = side;
	desc.Height = side;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA data{};
	data.pSysMem = pixels.data();
	data.SysMemPitch = side * 4;

	ID3D11Texture2D* texture = nullptr;
	if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &data, &texture)) || !texture)
		return false;

	const HRESULT result = g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &logo_texture);
	texture->Release();
	return SUCCEEDED(result) && logo_texture != nullptr;
}

void fill_wave(ImVec2 start, ImVec2 c1, ImVec2 c2, ImVec2 end, float bottom, const ImVec4& color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathLineTo(ImVec2(start.x, bottom));
	draw->PathLineTo(start);
	draw->PathBezierCubicCurveTo(c1, c2, end, 32);
	draw->PathLineTo(ImVec2(end.x, bottom));
	draw->PathFillConcave(color_u32(color));
}

void draw_login_waves(float w, float h, float time)
{
	const float s = std::sin(time * 0.5f) * 8.0f;

	fill_wave(ImVec2(0.0f, h - 150.0f), ImVec2(90.0f, h - 160.0f + s), ImVec2(170.0f, h - 30.0f),
		ImVec2(330.0f, h), h, fade(kLoginBlue, 0.10f));
	fill_wave(ImVec2(w, h - 140.0f), ImVec2(w - 90.0f, h - 150.0f - s), ImVec2(w - 180.0f, h - 20.0f),
		ImVec2(w - 340.0f, h), h, fade(kLoginBlue, 0.09f));
	fill_wave(ImVec2(0.0f, h - 70.0f), ImVec2(w * 0.35f, h - 20.0f + s), ImVec2(w * 0.65f, h - 95.0f - s),
		ImVec2(w, h - 55.0f), h, fade(kLoginPurple, 0.06f));
}

// One-line confirmation after the update helper restarted this build. It fades
// out on its own so it never covers the form. The hold starts on the form, so
// the startup check cannot use up the visible time.
void draw_notice(bool start_timer)
{
	if (notice_text.empty())
		return;

	const double now = ImGui::GetTime();
	if (start_timer && notice_time < 0.0)
		notice_time = now;

	float alpha = 1.0f;
	if (notice_time >= 0.0)
	{
		constexpr double kHoldSeconds = 7.0;
		constexpr double kFadeSeconds = 0.8;
		const double elapsed = now - notice_time;
		if (elapsed >= kHoldSeconds + kFadeSeconds)
		{
			notice_text.clear();
			return;
		}
		if (elapsed > kHoldSeconds)
			alpha = static_cast<float>(1.0 - (elapsed - kHoldSeconds) / kFadeSeconds);
	}

	const float w = text_width(font.body, 16.0f, notice_text.c_str());
	text(font.body, 16.0f, ImVec2(app::kLoginWidth * 0.5f - w * 0.5f, 232.0f),
		fade(kGreen, alpha), notice_text.c_str());
}

enum class StepState { Pending, Active, Done, Failed };

void step_indicator(ImVec2 c, StepState step)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float radius = 10.0f;

	switch (step)
	{
	case StepState::Done:
	{
		const int start = draw->VtxBuffer.Size;
		draw->AddCircleFilled(c, radius, IM_COL32_WHITE, 24);
		ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size,
			ImVec2(c.x - radius, c.y), ImVec2(c.x + radius, c.y), color_u32(kLoginBlue), color_u32(kLoginPurple));
		draw->AddLine(ImVec2(c.x - 4.5f, c.y + 0.5f), ImVec2(c.x - 1.5f, c.y + 3.5f), IM_COL32_WHITE, 2.0f);
		draw->AddLine(ImVec2(c.x - 1.5f, c.y + 3.5f), ImVec2(c.x + 4.5f, c.y - 3.5f), IM_COL32_WHITE, 2.0f);
		break;
	}
	case StepState::Active:
	{
		const float t = static_cast<float>(ImGui::GetTime());
		draw->AddCircle(c, radius, color_u32(fade(kLoginPurple, 0.35f)), 24, 2.0f);
		draw->PathArcTo(c, radius, t * 6.0f, t * 6.0f + IM_PI * 0.7f, 16);
		draw->PathStroke(color_u32(lift(kLoginPurple, 0.15f)), 0, 2.0f);
		const float pulse = 0.75f + 0.25f * std::sin(t * 5.0f);
		draw->AddCircleFilled(c, 4.0f * pulse, color_u32(kLoginPurple), 16);
		break;
	}
	case StepState::Failed:
		draw->AddCircleFilled(c, radius, color_u32(kRed), 24);
		draw->AddLine(ImVec2(c.x - 3.5f, c.y - 3.5f), ImVec2(c.x + 3.5f, c.y + 3.5f), IM_COL32_WHITE, 2.0f);
		draw->AddLine(ImVec2(c.x + 3.5f, c.y - 3.5f), ImVec2(c.x - 3.5f, c.y + 3.5f), IM_COL32_WHITE, 2.0f);
		break;
	default:
		draw->AddCircle(c, radius, color_u32(fade(kLoginIcon, 0.45f)), 24, 1.6f);
		break;
	}
}

} // namespace

// ------------------------------------------------------------------- logo ----

void EnsureLogo()
{
	if (logo_texture)
		return;

	const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
	if (now < logo_next_try)
		return;

	if (!load_logo())
		logo_next_try = now + kLogoRetrySeconds;
}

void ReleaseLogo()
{
	if (logo_texture)
	{
		logo_texture->Release();
		logo_texture = nullptr;
	}
}

ID3D11ShaderResourceView* Logo()
{
	return logo_texture;
}

void DrawLogo(ImVec2 c, float side)
{
	EnsureLogo();

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float half = side * 0.5f;
	if (logo_texture)
	{
		draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(logo_texture)),
			ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half));
		return;
	}

	// Fallback while the logo cannot load: a gradient tile with an "S".
	const Rect tile = rect(c.x - half, c.y - half, c.x + half, c.y + half);
	gradient_box(tile, kLoginBlue, kLoginPurple, side * 0.22f);
	text_center(font.title, side * 0.55f, tile, kText, "S");
}

// ------------------------------------------------------------------ frame ----

void SetNotice(const std::string& value)
{
	notice_text = value;
	notice_time = -1.0;
}

void DrawFrame(bool form_visible)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float w = app::kLoginWidth;
	const float h = app::kLoginHeight;
	const float cx = w * 0.5f;

	draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, h), color_u32(kLoginCard));
	draw_login_waves(w, h, static_cast<float>(ImGui::GetTime()));
	draw->AddRect(ImVec2(0.5f, 0.5f), ImVec2(w - 0.5f, h - 0.5f), color_u32(kLoginBorder), 8.0f, 0, 1.0f);

	// Drag strip and window buttons.
	ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
	ImGui::InvisibleButton("##login_drag", ImVec2(w - 84.0f, 44.0f));
	if (ImGui::IsItemClicked())
		StartWindowDrag();

	if (window_button("##login_minimize", rect(w - 78.0f, 8.0f, w - 46.0f, 40.0f), false))
		ShowWindow(g_hwnd, SW_MINIMIZE);
	if (window_button("##login_close", rect(w - 40.0f, 8.0f, w - 8.0f, 40.0f), true))
		PostQuitMessage(0);

	DrawLogo(ImVec2(cx, 98.0f), static_cast<float>(kLogoSize));

	// Title.
	const float title_size = 40.0f;
	const float first_w = text_width(font.title, title_size, "Cloth ");
	const float title_w = first_w + text_width(font.title, title_size, "Changer");
	const ImVec2 title_pos(cx - title_w * 0.5f, 158.0f);
	text(font.title, title_size, title_pos, kText, "Cloth ");
	gradient_text(font.title, title_size, ImVec2(title_pos.x + first_w, title_pos.y), "Changer",
		kLoginPurple, lift(kLoginBlue, 0.12f));

	const char* subtitle = "Free Fire Skin Changer";
	const float subtitle_w = text_width(font.body, 18.0f, subtitle);
	text(font.body, 18.0f, ImVec2(cx - subtitle_w * 0.5f, 208.0f), kLoginSubtitle, subtitle);

	const char* footer = "Fast  -  Stable  -  Private";
	const float footer_w = text_width(font.body, 16.0f, footer);
	const float footer_y = h - 46.0f < 540.0f ? 540.0f : h - 46.0f;
	text(font.body, 16.0f, ImVec2(cx - footer_w * 0.5f, footer_y), fade(kLoginSubtitle, 0.85f), footer);

	draw_notice(form_visible);
}

// ---------------------------------------------------------------- loading ----

void StartAnim(LoadingAnim& anim)
{
	anim = LoadingAnim();
	anim.step_time = ImGui::GetTime();
}

void Advance(LoadingAnim& anim, int real_active, int count, double step_seconds)
{
	const double now = ImGui::GetTime();
	if (anim.shown < real_active && now - anim.step_time >= step_seconds)
	{
		++anim.shown;
		anim.step_time = now;
	}

	// The bar creeps inside the active step so it never looks frozen.
	float target = static_cast<float>(anim.shown) / static_cast<float>(count);
	if (anim.shown < count)
	{
		const float creep_seconds = step_seconds > 1.6 ? static_cast<float>(step_seconds) : 1.6f;
		const float creep = clampf(static_cast<float>(now - anim.step_time) / creep_seconds, 0.0f, 0.8f);
		target += creep / static_cast<float>(count);
	}

	anim.progress += (target - anim.progress) * clampf(ImGui::GetIO().DeltaTime * 6.0f, 0.0f, 1.0f);
	if (anim.shown >= count && anim.progress > 0.995f)
	{
		anim.progress = 1.0f;
		if (anim.done_time < 0.0)
			anim.done_time = now;
	}
}

bool Finished(const LoadingAnim& anim)
{
	constexpr double kDoneHoldSeconds = 0.15;
	return anim.done_time >= 0.0 && ImGui::GetTime() - anim.done_time >= kDoneHoldSeconds;
}

void DrawLoading(const char* const* steps, int count, const LoadingAnim& anim, int failed_step,
	const std::string& caption, bool caption_is_error)
{
	const float w = app::kLoginWidth;
	const float cx = w * 0.5f;
	const float fx = 70.0f;

	for (int i = 0; i < count; ++i)
	{
		StepState step = StepState::Pending;
		if (i == failed_step)
			step = StepState::Failed;
		else if (i < anim.shown)
			step = StepState::Done;
		else if (i == anim.shown && failed_step < 0)
			step = StepState::Active;

		const float y = 262.0f + i * 42.0f;
		step_indicator(ImVec2(fx + 10.0f, y + 10.0f), step);

		ImVec4 color = lift(kLoginIcon, 0.18f);
		if (step == StepState::Active)
			color = kText;
		else if (step == StepState::Pending)
			color = fade(kLoginIcon, 0.6f);
		else if (step == StepState::Failed)
			color = kRed;
		text(font.body, 18.0f, ImVec2(fx + 34.0f, y), color, steps[i]);
	}

	// Progress bar.
	const Rect track = rect(60.0f, 410.0f, w - 60.0f, 418.0f);
	box(track, kLoginField, 4.0f);
	const float fill = clampf(anim.progress, 0.0f, 1.0f);
	if (fill > 0.01f)
	{
		const Rect bar = rect(track.min.x, track.min.y, track.min.x + width(track) * fill, track.max.y);
		gradient_box(bar, failed_step >= 0 ? kRed : kLoginBlue, failed_step >= 0 ? kRed : kLoginPurple, 4.0f);
	}

	char percent[16];
	std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(fill * 100.0f + 0.5f));
	const float pw = text_width(font.body, 15.0f, percent);
	text(font.body, 15.0f, ImVec2(track.max.x - pw, 426.0f), kLoginHint, percent);

	if (!caption.empty())
	{
		const ImVec4 color = caption_is_error ? kRed : kLoginSubtitle;
		const float mw = text_width(font.body, 15.0f, caption.c_str());
		const float x = caption_is_error ? cx - mw * 0.5f : track.min.x;
		text(font.body, 15.0f, ImVec2(x, caption_is_error ? 446.0f : 426.0f), color, caption.c_str());
	}
}

// ---------------------------------------------------------------- buttons ----

bool GradientButton(const char* id, const Rect& r, const char* value)
{
	ImGui::SetCursorScreenPos(r.min);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(width(r), height(r)));
	const bool hovered = ImGui::IsItemHovered();
	const bool held = ImGui::IsItemActive();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	const float shift = held ? -0.04f : (hovered ? 0.06f : 0.0f);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (hovered)
		draw->AddRect(ImVec2(r.min.x - 2.0f, r.min.y - 2.0f), ImVec2(r.max.x + 2.0f, r.max.y + 2.0f),
			color_u32(fade(kLoginPurple, 0.30f)), 12.0f, 0, 3.0f);
	gradient_box(r, lift(kLoginBlue, shift), lift(kLoginPurple, shift), 10.0f);

	const float size = 20.0f;
	const float label_w = text_width(font.semibold, size, value);
	const float arrow_w = 16.0f;
	const float total = label_w + 12.0f + arrow_w;
	const float x = r.min.x + (width(r) - total) * 0.5f;
	const float y = r.min.y + (height(r) - size) * 0.5f - 1.0f;
	text(font.semibold, size, ImVec2(x, y), kText, value);

	const ImU32 white = IM_COL32_WHITE;
	const float ax = x + label_w + 12.0f;
	const float ay = r.min.y + height(r) * 0.5f;
	draw->AddLine(ImVec2(ax, ay), ImVec2(ax + arrow_w, ay), white, 2.0f);
	draw->AddLine(ImVec2(ax + arrow_w - 6.0f, ay - 6.0f), ImVec2(ax + arrow_w, ay), white, 2.0f);
	draw->AddLine(ImVec2(ax + arrow_w - 6.0f, ay + 6.0f), ImVec2(ax + arrow_w, ay), white, 2.0f);
	return clicked;
}

void BusyButton(const Rect& r, const char* value)
{
	gradient_box(r, fade(kLoginBlue, 0.45f), fade(kLoginPurple, 0.45f), 10.0f);
	text_center(font.semibold, 20.0f, r, fade(kText, 0.75f), value);
}

} // namespace login_ui
