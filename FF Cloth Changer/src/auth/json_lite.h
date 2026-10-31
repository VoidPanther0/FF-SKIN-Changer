#pragma once

// Minimal read-only JSON parser.
// Enough for the small Supabase responses this loader reads:
// objects, arrays, strings, numbers, booleans and null.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace jsonlite {

struct Value {
	enum class Type { Null, Bool, Number, String, Array, Object };

	Type type = Type::Null;
	bool boolean = false;
	double number = 0.0;
	std::string text;
	std::vector<Value> array;
	std::vector<std::pair<std::string, Value>> object;

	bool is_null() const { return type == Type::Null; }
	bool is_object() const { return type == Type::Object; }
	bool is_array() const { return type == Type::Array; }
	bool is_string() const { return type == Type::String; }
	bool is_number() const { return type == Type::Number; }

	const Value* find(const char* name) const {
		if (type != Type::Object)
			return nullptr;

		for (const auto& pair : object) {
			if (pair.first == name)
				return &pair.second;
		}
		return nullptr;
	}

	std::string get_string(const char* name, const std::string& fallback = std::string()) const {
		const Value* v = find(name);
		if (!v)
			return fallback;
		if (v->type == Type::String)
			return v->text;
		if (v->type == Type::Number) {
			char buf[64];
			sprintf_s(buf, "%.0f", v->number);
			return std::string(buf);
		}
		return fallback;
	}

	double get_number(const char* name, double fallback = 0.0) const {
		const Value* v = find(name);
		if (!v)
			return fallback;
		if (v->type == Type::Number)
			return v->number;
		if (v->type == Type::String)
			return std::atof(v->text.c_str());
		return fallback;
	}

	bool get_bool(const char* name, bool fallback = false) const {
		const Value* v = find(name);
		if (!v)
			return fallback;
		if (v->type == Type::Bool)
			return v->boolean;
		if (v->type == Type::Number)
			return v->number != 0.0;
		return fallback;
	}
};

class Parser {
public:
	explicit Parser(const std::string& input) : src_(input) {}

	bool parse(Value& out) {
		skip_ws();
		if (!parse_value(out, 0))
			return false;
		skip_ws();
		return true;
	}

private:
	const std::string& src_;
	std::size_t pos_ = 0;

	void skip_ws() {
		while (pos_ < src_.size()) {
			const char c = src_[pos_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				++pos_;
			else
				break;
		}
	}

	bool parse_value(Value& out, int depth) {
		if (depth > 32)
			return false;

		skip_ws();
		if (pos_ >= src_.size())
			return false;

		switch (src_[pos_]) {
		case '{': return parse_object(out, depth);
		case '[': return parse_array(out, depth);
		case '"':
			out.type = Value::Type::String;
			return parse_string(out.text);
		case 't': return parse_literal("true", out, true);
		case 'f': return parse_literal("false", out, false);
		case 'n': return parse_literal("null", out, false);
		default: return parse_number(out);
		}
	}

	bool parse_object(Value& out, int depth) {
		out.type = Value::Type::Object;
		++pos_; // '{'

		skip_ws();
		if (pos_ < src_.size() && src_[pos_] == '}')
			return ++pos_, true;

		for (;;) {
			skip_ws();
			if (pos_ >= src_.size())
				return false;

			std::string key;
			if (!parse_string(key))
				return false;

			skip_ws();
			if (!consume(':'))
				return false;

			Value item;
			if (!parse_value(item, depth + 1))
				return false;
			out.object.emplace_back(std::move(key), std::move(item));

			skip_ws();
			if (consume(','))
				continue;
			if (consume('}'))
				return true;
			return false;
		}
	}

	bool parse_array(Value& out, int depth) {
		out.type = Value::Type::Array;
		++pos_; // '['

		skip_ws();
		if (pos_ < src_.size() && src_[pos_] == ']')
			return ++pos_, true;

		for (;;) {
			Value item;
			if (!parse_value(item, depth + 1))
				return false;
			out.array.push_back(std::move(item));

			skip_ws();
			if (consume(','))
				continue;
			if (consume(']'))
				return true;
			return false;
		}
	}

	bool parse_string(std::string& out) {
		if (!consume('"'))
			return false;

		out.clear();
		while (pos_ < src_.size()) {
			const char c = src_[pos_++];

			if (c == '"')
				return true;

			if (c != '\\') {
				out.push_back(c);
				continue;
			}

			if (pos_ >= src_.size())
				return false;

			const char esc = src_[pos_++];
			switch (esc) {
			case '"': out.push_back('"'); break;
			case '\\': out.push_back('\\'); break;
			case '/': out.push_back('/'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'n': out.push_back('\n'); break;
			case 'r': out.push_back('\r'); break;
			case 't': out.push_back('\t'); break;
			case 'u': {
				unsigned int cp = 0;
				if (!parse_hex4(cp))
					return false;
				append_utf8(out, cp);
				break;
			}
			default: return false;
			}
		}
		return false;
	}

	bool parse_hex4(unsigned int& out) {
		if (pos_ + 4 > src_.size())
			return false;

		unsigned int value = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = src_[pos_++];
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= static_cast<unsigned int>(c - '0');
			else if (c >= 'a' && c <= 'f')
				value |= static_cast<unsigned int>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				value |= static_cast<unsigned int>(c - 'A' + 10);
			else
				return false;
		}
		out = value;
		return true;
	}

	static void append_utf8(std::string& out, unsigned int cp) {
		if (cp <= 0x7F) {
			out.push_back(static_cast<char>(cp));
		}
		else if (cp <= 0x7FF) {
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else {
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	bool parse_number(Value& out) {
		const char* begin = src_.c_str() + pos_;
		char* end = nullptr;
		const double value = std::strtod(begin, &end);
		if (end == begin)
			return false;

		pos_ += static_cast<std::size_t>(end - begin);
		out.type = Value::Type::Number;
		out.number = value;
		return true;
	}

	bool parse_literal(const char* literal, Value& out, bool value) {
		const std::size_t len = std::char_traits<char>::length(literal);
		if (src_.compare(pos_, len, literal) != 0)
			return false;

		pos_ += len;
		out.type = (literal[0] == 'n') ? Value::Type::Null : Value::Type::Bool;
		out.boolean = value;
		return true;
	}

	bool consume(char c) {
		if (pos_ < src_.size() && src_[pos_] == c) {
			++pos_;
			return true;
		}
		return false;
	}
};

// Parses text. Returns false only when the text is not valid JSON.
inline bool Parse(const std::string& text, Value& out) {
	Parser parser(text);
	return parser.parse(out);
}

} // namespace jsonlite
