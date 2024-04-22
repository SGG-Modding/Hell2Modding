#pragma once

namespace big::string
{
	inline bool starts_with(const char* pre, const char* str)
	{
		return strncmp(pre, str, strlen(pre)) == 0;
	}

	template<typename T>
	inline std::enable_if_t<std::is_same_v<T, std::string>, T> get_text_value(std::string text)
	{
		return text;
	}

	template<typename T>
	inline std::enable_if_t<!std::is_same_v<T, std::string>, T> get_text_value(std::string text)
	{
		T value = (T)0;
		std::stringstream(text) >> value;
		return value;
	}

	template<typename T = std::string>
	inline std::vector<T> split(const std::string& text, const char delim)
	{
		std::vector<T> result;
		std::string str;
		std::stringstream ss(text);
		while (std::getline(ss, str, delim))
		{
			result.push_back(get_text_value<T>(str));
		}
		return result;
	}

	inline std::string replace(const std::string& original, const std::string& old_value, const std::string& new_value)
	{
		std::string result = original;
		size_t pos         = 0;

		while ((pos = result.find(old_value, pos)) != std::string::npos)
		{
			result.replace(pos, old_value.length(), new_value);
			pos += new_value.length();
		}

		return result;
	}

	inline std::string to_lower(const std::string& input)
	{
		std::string result = input;

		// Use the std::locale to ensure proper case conversion for the current locale
		std::locale loc;

		for (size_t i = 0; i < result.length(); ++i)
		{
			result[i] = std::tolower(result[i], loc);
		}

		return result;
	}
} // namespace big::string
