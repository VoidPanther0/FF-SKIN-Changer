#include "common/dashboard_common.h"

#include "../../cloth/cloth_changer.h"
#include "../../cloth/offsets.h"

#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Wardrobe page.
//
// Left card: the wardrobe as the game currently holds it. These are the real
// loaded ids, read back from memory, so picking a slot here is the only
// honest way to choose the side a swap overwrites.
//
// Right card: the downloaded catalog, filtered by category and search. Picking
// an entry there chooses what the selected slot should render.
//
// Bottom strip: every swap that is currently recorded, with a remove button
// per swap and one clear-all.
// ---------------------------------------------------------------------------

namespace dash {

using namespace ui;

namespace {

// -------------------------------------------------------------- layout -----

constexpr float kStatusTop = 88.0f;
constexpr float kStatusBottom = 132.0f;

constexpr float kCardsTop = 144.0f;
constexpr float kCardsBottom = 440.0f;
constexpr float kColumnGap = 12.0f;
constexpr float kColumnWidth = (kContentRight - kContentLeft - kColumnGap) * 0.5f;

constexpr float kAppliedTop = 452.0f;
constexpr float kAppliedBottom = kContentBottom;

constexpr float kSlotRow = 30.0f;
constexpr float kItemRow = 28.0f;

// ---------------------------------------------------------------- state -----

const cloth::Category kCategories[] = {
	cloth::Category::Top,
	cloth::Category::Bottom,
	cloth::Category::Shoes,
	cloth::Category::Head,
	cloth::Category::Facepaint,
	cloth::Category::Mask,
	cloth::Category::Other,
};

constexpr int kCategoryCount = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));

int g_category = 0;
char g_search[48] = "";

int g_selected_slot = -1;     // index into g_slots
int g_selected_item = -1;     // index into g_items

std::vector<cloth::Slot> g_slots;
std::vector<cloth::Entry> g_items;
std::vector<std::string> g_labels;

// Cached catalog query. Rebuilt only when the query or the catalog changes,
// so typing in the search box does not re-filter on every frame.
int g_cached_size = -1;
int g_cached_category = -1;
std::string g_cached_search;

bool g_catalog_started = false;

void refresh_slots()
{
	g_slots = cloth::WardrobeSlots();

	// A slot that vanished (the game reloaded its table) drops its selection
	// instead of leaving an index pointing past the list.
	if (g_selected_slot >= static_cast<int>(g_slots.size()))
		g_selected_slot = -1;
}

void refresh_catalog()
{
	const int size = cloth::CatalogSize();
	const std::string search = g_search;

	if (size == g_cached_size && search == g_cached_search && g_category == g_cached_category)
		return;

	g_items = cloth::Catalog(kCategories[g_category], search.c_str());
	g_labels = cloth::BuildLabels(g_items);

	g_cached_size = size;
	g_cached_search = search;
	g_cached_category = g_category;

	if (g_selected_item >= static_cast<int>(g_items.size()))
		g_selected_item = -1;
}

// -------------------------------------------------------------- widgets ----

bool chip(const char* label, const Rect& r, bool selected)
{
	ImGui::SetCursorScreenPos(r.min);
	ImGui::PushID(label);
	const bool clicked = ImGui::InvisibleButton("##chip", ImVec2(width(r), height(r)));
	const bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();

	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	if (selected)
		gradient_box(r, accent_color(), kPurple, height(r) * 0.5f);
	else
		box(r, hovered ? kPanel3 : kCardSoft, height(r) * 0.5f, selected ? nullptr : &kLine);

	const ImVec4 color = selected ? kWhite : kMuted;
	text_center(font.caption, 10.0f, r, color, label);
	return clicked;
}

// Compact switch used by the status strip; the full toggle_row is too tall
// for the one-line strip.
bool compact_toggle(const Rect& r, bool* value)
{
	ImGui::SetCursorScreenPos(r.min);
	const bool clicked = ImGui::InvisibleButton("##cloth_enabled", ImVec2(width(r), height(r)));
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	if (clicked)
		*value = !*value;

	const float sw = 40.0f;
	const float sh = 20.0f;
	const Rect track = rect(r.max.x - sw, r.min.y + (height(r) - sh) * 0.5f,
		r.max.x, r.min.y + (height(r) + sh) * 0.5f);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	box(track, *value ? accent_color() : kPanel3, sh * 0.5f);
	const float knob_x = *value ? track.max.x - sh * 0.5f : track.min.x + sh * 0.5f;
	draw->AddCircleFilled(ImVec2(knob_x, track.min.y + sh * 0.5f), sh * 0.5f - 3.0f, color_u32(kText), 16);

	text(font.semibold, 13.0f, ImVec2(r.min.x, r.min.y + (height(r) - 13.0f) * 0.5f),
		*value ? kText : kMuted, "Apply swaps");
	return clicked;
}

// ---------------------------------------------------------------- status ---

void draw_status()
{
	// The badge reports what the module really knows: attached with a base,
	// attached without one, or not attached at all.
	const cloth::Status status = cloth::AttachStatus();
	ImVec4 color = kMuted;
	const char* label = "NOT ATTACHED";
	if (status == cloth::Status::Ok)
	{
		color = kGreen;
		label = "ATTACHED";
	}
	else if (status == cloth::Status::Error)
	{
		color = kRed;
		label = "ATTACH FAILED";
	}

	status_badge(rect(kContentLeft, kStatusTop + 8.0f, kContentLeft + 122.0f, kStatusBottom - 8.0f),
		label, color);

	// The reason sits next to the badge so a failure explains itself without
	// a tooltip.
	const char* detail = cloth::AttachMessage();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(ImVec2(kContentLeft + 134.0f, kStatusTop), ImVec2(kContentLeft + 330.0f, kStatusBottom), true);
	text(font.caption, 10.0f, ImVec2(kContentLeft + 134.0f, kStatusTop + 15.0f), kDim, detail);
	draw->PopClipRect();

	bool enabled = cloth::Enabled();
	if (compact_toggle(rect(kContentRight - 150.0f, kStatusTop, kContentRight, kStatusBottom), &enabled))
		cloth::SetEnabled(enabled);

	// Reload only ever runs from the button; nothing downloads on its own.
	if (card_button("Reload", rect(kContentRight - 250.0f, kStatusTop + 6.0f, kContentRight - 160.0f, kStatusBottom - 6.0f)))
		cloth::ReloadCatalog(offsets::catalog::kDefaultUrl);
}

// ----------------------------------------------------------- live slots ----

void draw_slots()
{
	const Rect card = rect(kContentLeft, kCardsTop, kContentLeft + kColumnWidth, kCardsBottom);
	box(card, kCard, 14.0f, card_border());
	section_header(card, "Your wardrobe", icon_hanger);

	char count[32];
	std::snprintf(count, sizeof(count), "%d loaded", static_cast<int>(g_slots.size()));
	const float count_w = text_width(font.caption, 10.0f, count);
	text(font.caption, 10.0f, ImVec2(card.max.x - 16.0f - count_w, card.min.y + 28.0f), kDim, count);

	divider(card.min.x + 20.0f, card.max.x - 20.0f, card.min.y + 62.0f);

	const Rect area = rect(card.min.x + 14.0f, card.min.y + 72.0f, card.max.x - 14.0f, card.max.y - 14.0f);

	if (g_slots.empty())
	{
		// An empty list here always means the game is still loading, never
		// that the wardrobe has no items.
		const bool readable = cloth::WardrobeReadable();
		draw_empty_state(area, readable ? "No items" : "Waiting for the game",
			readable ? "The wardrobe list came back empty."
					 : "Enter a match or the lobby, then this fills in.");
		return;
	}

	ImGui::SetCursorScreenPos(area.min);
	ImGui::BeginChild("##slot_rows", ImVec2(width(area), height(area)), 0, ImGuiWindowFlags_NoBackground);
	const ImVec2 origin = ImGui::GetCursorScreenPos();

	const int count_slots = static_cast<int>(g_slots.size());
	for (int i = 0; i < count_slots; ++i)
	{
		const Rect row = rect(origin.x, origin.y + static_cast<float>(i) * kSlotRow,
			origin.x + width(area) - 6.0f, origin.y + static_cast<float>(i) * kSlotRow + kSlotRow - 4.0f);

		const bool selected = i == g_selected_slot;

		ImGui::PushID(i);
		ImGui::SetCursorScreenPos(row.min);
		const bool clicked = ImGui::InvisibleButton("##slot", ImVec2(width(row), height(row)));
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		if (hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		if (selected)
			gradient_box(row, fade(accent_color(), 0.22f), fade(kPurple, 0.22f), 8.0f);
		else if (hovered)
			box(row, kCardSoft, 8.0f);
		else
			box(row, fade(kCardSoft, 0.55f), 8.0f);

		char index[24];
		std::snprintf(index, sizeof(index), "Slot %d", g_slots[static_cast<std::size_t>(i)].index);
		text(font.semibold, 12.0f, ImVec2(row.min.x + 12.0f, row.min.y + 6.0f),
			selected ? kWhite : kText, index);

		char id[24];
		std::snprintf(id, sizeof(id), "%u", g_slots[static_cast<std::size_t>(i)].item_id);
		const float id_w = text_width(font.caption, 10.0f, id);
		text(font.caption, 10.0f, ImVec2(row.max.x - 12.0f - id_w, row.min.y + 8.0f), kDim, id);

		if (clicked)
			g_selected_slot = i;
	}

	ImGui::SetCursorScreenPos(origin);
	ImGui::Dummy(ImVec2(1.0f, static_cast<float>(count_slots) * kSlotRow));
	ImGui::EndChild();
}

// -------------------------------------------------------------- picker -----

void draw_picker()
{
	const Rect card = rect(kContentLeft + kColumnWidth + kColumnGap, kCardsTop, kContentRight, kCardsBottom);
	box(card, kCard, 14.0f, card_border());
	section_header(card, "Pick an item", icon_components);

	const cloth::Status status = cloth::CatalogStatus();
	if (status == cloth::Status::Working)
	{
		spinner(ImVec2(card.max.x - 28.0f, card.min.y + 33.0f), accent_color());
	}
	else
	{
		char ready[32];
		std::snprintf(ready, sizeof(ready), "%d items", cloth::CatalogSize());
		const float w = text_width(font.caption, 10.0f, ready);
		const ImVec4 color = status == cloth::Status::Error ? kRed : kDim;
		text(font.caption, 10.0f, ImVec2(card.max.x - 16.0f - w, card.min.y + 28.0f), color, ready);
	}

	divider(card.min.x + 20.0f, card.max.x - 20.0f, card.min.y + 62.0f);

	// Category chips wrap into two rows of four.
	const float chip_h = 22.0f;
	const float chip_gap = 6.0f;
	const float chip_w = (width(card) - 40.0f - chip_gap * 3.0f) * 0.25f;

	for (int i = 0; i < kCategoryCount; ++i)
	{
		const int row = i / 4;
		const int column = i % 4;
		const float x = card.min.x + 20.0f + static_cast<float>(column) * (chip_w + chip_gap);
		const float y = card.min.y + 74.0f + static_cast<float>(row) * (chip_h + chip_gap);

		if (chip(cloth::CategoryName(kCategories[i]), rect(x, y, x + chip_w, y + chip_h), i == g_category))
			g_category = i;
	}

	const Rect search = rect(card.min.x + 20.0f, card.min.y + 132.0f, card.max.x - 20.0f, card.min.y + 162.0f);
	box(search, kCardSoft, 9.0f, &kLine);
	icon_search(ImVec2(search.min.x + 18.0f, center(search).y), color_u32(kNavText));

	constexpr float font_size = 12.0f;
	ImGui::PushFont(font.body, font_size);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (height(search) - font_size) * 0.5f - 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_TextDisabled, kDim);

	ImGui::SetCursorScreenPos(ImVec2(search.min.x + 34.0f, search.min.y));
	ImGui::SetNextItemWidth(width(search) - 44.0f);
	ImGui::InputTextWithHint("##cloth_search", "Search items...", g_search, sizeof(g_search));

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
	ImGui::PopFont();

	const Rect list = rect(card.min.x + 20.0f, card.min.y + 172.0f, card.max.x - 20.0f, card.max.y - 62.0f);

	if (g_items.empty())
	{
		const bool working = cloth::CatalogStatus() == cloth::Status::Working;
		const char* title = working ? "Loading" : "Nothing found";
		const char* subtitle = working ? "Downloading the item list..."
									   : "Set the catalog URL in cloth/offsets.h, then Reload.";
		draw_empty_state(list, title, subtitle);
	}
	else
	{
		ImGui::SetCursorScreenPos(list.min);
		ImGui::BeginChild("##item_rows", ImVec2(width(list), height(list)), 0, ImGuiWindowFlags_NoBackground);
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		const int count_items = static_cast<int>(g_items.size());
		for (int i = 0; i < count_items; ++i)
		{
			const Rect row = rect(origin.x, origin.y + static_cast<float>(i) * kItemRow,
				origin.x + width(list) - 6.0f, origin.y + static_cast<float>(i) * kItemRow + kItemRow - 4.0f);

			const bool selected = i == g_selected_item;

			ImGui::PushID(i);
			ImGui::SetCursorScreenPos(row.min);
			const bool clicked = ImGui::InvisibleButton("##item", ImVec2(width(row), height(row)));
			const bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();

			if (hovered)
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

			if (selected)
				gradient_box(row, fade(accent_color(), 0.22f), fade(kPurple, 0.22f), 8.0f);
			else if (hovered)
				box(row, kCardSoft, 8.0f);
			else
				box(row, fade(kCardSoft, 0.55f), 8.0f);

			const cloth::Entry& entry = g_items[static_cast<std::size_t>(i)];
			const std::string& label = g_labels[static_cast<std::size_t>(i)];

			// The name is clipped to the row so a long description cannot run
			// into the id on the right.
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(ImVec2(row.min.x + 12.0f, row.min.y), ImVec2(row.max.x - 86.0f, row.max.y), true);
			text(font.semibold, 12.0f, ImVec2(row.min.x + 12.0f, row.min.y + 5.0f),
				selected ? kWhite : kText, label.c_str());
			draw->PopClipRect();

			char id[24];
			std::snprintf(id, sizeof(id), "%u", entry.item_id);
			const float id_w = text_width(font.caption, 10.0f, id);
			text(font.caption, 10.0f, ImVec2(row.max.x - 12.0f - id_w, row.min.y + 7.0f), kDim, id);

			if (clicked)
				g_selected_item = i;
		}

		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(1.0f, static_cast<float>(count_items) * kItemRow));
		ImGui::EndChild();
	}

	// The swap needs both sides: a slot from the left and an item from the
	// right. Until both are picked the button stays out of the way rather
	// than failing on click.
	const bool ready = g_selected_slot >= 0 && g_selected_item >= 0
		&& g_selected_slot < static_cast<int>(g_slots.size())
		&& g_selected_item < static_cast<int>(g_items.size());

	if (!ready)
		return;

	const Rect apply = rect(card.min.x + 20.0f, card.max.y - 52.0f, card.max.x - 20.0f, card.max.y - 16.0f);
	if (primary_button("##cloth_apply", apply, "Apply to slot", icon_hanger))
	{
		const cloth::Slot& slot = g_slots[static_cast<std::size_t>(g_selected_slot)];
		const cloth::Entry& entry = g_items[static_cast<std::size_t>(g_selected_item)];
		if (cloth::Apply(slot.item_id, entry.item_id))
			toast("Swap recorded. It is written on the next commit.");
		else
			toast_error("That swap could not be recorded.");
	}
}

// -------------------------------------------------------------- applied ----

void draw_applied()
{
	const std::vector<cloth::Applied> applied = cloth::AppliedList();

	const Rect strip = rect(kContentLeft, kAppliedTop, kContentRight, kAppliedBottom);
	box(strip, kCard, 12.0f, card_border());

	ImDrawList* draw = ImGui::GetWindowDrawList();

	if (applied.empty())
	{
		text(font.caption, 11.0f, ImVec2(strip.min.x + 16.0f, center(strip).y - 5.5f), kDim,
			"Nothing applied yet. Pick a slot and an item above.");
		return;
	}

	// The clear button owns the right end; pills stop short of it.
	const Rect clear = rect(strip.max.x - 92.0f, strip.min.y + 6.0f, strip.max.x - 8.0f, strip.max.y - 6.0f);

	// Each pill carries the swap and its own remove button; a swap the game
	// has not reached yet is drawn dimmer so it reads as pending.
	float x = strip.min.x + 12.0f;
	const float pill_h = 24.0f;
	const float pill_y = strip.min.y + (height(strip) - pill_h) * 0.5f;

	const int live = cloth::LiveCount();
	const int total = static_cast<int>(applied.size());
	const int room = static_cast<int>((clear.min.x - 16.0f - x) / 132.0f);
	const int shown = total < room ? total : room;

	for (int i = 0; i < shown; ++i)
	{
		const cloth::Applied& entry = applied[static_cast<std::size_t>(i)];
		const bool pending = i >= live;

		char label[64];
		std::snprintf(label, sizeof(label), "%u  \xE2\x86\x92  %u", entry.owned_id, entry.target_id);

		const Rect swap_rect = rect(x, pill_y, x + 124.0f, pill_y + pill_h);
		box(swap_rect, pending ? fade(kCardSoft, 0.7f) : fade(accent_color(), 0.16f), pill_h * 0.5f,
			pending ? &kLine : nullptr);

		draw->PushClipRect(ImVec2(swap_rect.min.x + 10.0f, swap_rect.min.y), ImVec2(swap_rect.max.x - 26.0f, swap_rect.max.y), true);
		text(font.caption, 10.0f, ImVec2(swap_rect.min.x + 10.0f, pill_y + 7.0f), pending ? kMuted : kText, label);
		draw->PopClipRect();

		// The × is its own button so a click on the pill text cannot remove
		// a swap by accident.
		const Rect remove = rect(swap_rect.max.x - 22.0f, pill_y + 4.0f, swap_rect.max.x - 4.0f, pill_y + pill_h - 4.0f);
		ImGui::PushID(i);
		ImGui::SetCursorScreenPos(remove.min);
		const bool clicked = ImGui::InvisibleButton("##remove", ImVec2(width(remove), height(remove)));
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		if (hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		const ImVec2 c = center(remove);
		const ImU32 glyph = color_u32(hovered ? kRed : kDim);
		draw->AddLine(ImVec2(c.x - 3.5f, c.y - 3.5f), ImVec2(c.x + 3.5f, c.y + 3.5f), glyph, 1.5f);
		draw->AddLine(ImVec2(c.x + 3.5f, c.y - 3.5f), ImVec2(c.x - 3.5f, c.y + 3.5f), glyph, 1.5f);

		if (clicked)
			cloth::Remove(entry.owned_id);

		x += 132.0f;
	}

	if (total > shown)
	{
		char more[32];
		std::snprintf(more, sizeof(more), "+%d more", total - shown);
		const float w = text_width(font.caption, 10.0f, more);
		text(font.caption, 10.0f, ImVec2(clear.min.x - 12.0f - w, center(strip).y - 5.0f), kDim, more);
	}

	if (card_button("Clear all", clear))
		cloth::ClearAll();
}

} // namespace

void DrawCloth()
{
	// The catalog is fetched once when the page is first opened; after that
	// only the Reload button starts a download.
	if (!g_catalog_started)
	{
		g_catalog_started = true;
		cloth::ReloadCatalog(offsets::catalog::kDefaultUrl);
	}

	// The live wardrobe changes when the game reloads its table, so the list
	// is refreshed on a slow timer rather than every frame.
	static int frame = 0;
	if (++frame >= 60)
	{
		frame = 0;
		refresh_slots();
	}

	refresh_catalog();

	draw_status();
	draw_slots();
	draw_picker();
	draw_applied();

	// A commit that found nothing says why, and that reason is worth showing
	// once instead of hiding in the status bar.
	const char* reason = cloth::CommitMessage();
	if (reason[0] != '\0')
	{
		const float w = text_width(font.caption, 10.0f, reason);
		text(font.caption, 10.0f, ImVec2(kContentRight - w, kStatusTop + 15.0f), kOrange, reason);
	}
}

} // namespace dash
