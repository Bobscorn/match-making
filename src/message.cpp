#include "message.h"

std::vector<char> Message::to_bytes() const
{
	std::vector<char> out_data{ sizeof(Type) + sizeof(Length) + Data.size(), '0', std::allocator<char>() };
	std::memcpy(out_data.data(), this, sizeof(Type) + sizeof(Length));
	auto offset = sizeof(Type) + sizeof(Length);
	for (int i = 0; i < Data.size(); ++i)
		out_data[i + offset] = Data[i];
	return out_data;
}
