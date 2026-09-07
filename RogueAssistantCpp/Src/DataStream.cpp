#include "DataStream.h"
#include "Endian.h"

#include <algorithm>
#include <iterator>

DataStream::DataStream()
	: m_Pos(0)
	, m_IsWrite(true)
{
}

DataStream::DataStream(std::istream& inStream)
	: m_Pos(0)
	, m_IsWrite(false)
{
	for (std::istreambuf_iterator<char> it(inStream), end; it != end; ++it)
	{
		m_Data.push_back(static_cast<u8>(*it));
	}
}

template<typename T>
static bool SerializeInternalWrite(std::vector<u8>& data, std::size_t& pos, T val)
{
	rogue::endian::AppendLittle(data, val);
	pos += sizeof(T);
	return true;
}

template<typename T>
static bool SerializeInternalRead(std::vector<u8> const& data, std::size_t& pos, T& val)
{
	if (!rogue::endian::ReadLittle<T>(data, pos, val))
	{
		return false;
	}
	pos += sizeof(T);
	return true;
}

template<typename T>
static bool SerializeInternal(std::vector<u8>& data, std::size_t& pos, bool isWrite, T& val)
{
	if (isWrite)
		return SerializeInternalWrite(data, pos, val);
	else
		return SerializeInternalRead(data, pos, val);
}

bool DataStream::Serialize(u8& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}
bool DataStream::Serialize(s8& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}

bool DataStream::Serialize(u16& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}
bool DataStream::Serialize(s16& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}

bool DataStream::Serialize(u32& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}
bool DataStream::Serialize(s32& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}

bool DataStream::Serialize(u64& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}
bool DataStream::Serialize(s64& val)
{
	return SerializeInternal(m_Data, m_Pos, m_IsWrite, val);
}

bool DataStream::Serialize(u8* data, std::size_t size)
{
	if (size == 0)
	{
		return true;
	}

	if (data == nullptr)
	{
		return false;
	}

	if (m_IsWrite)
	{
		m_Data.insert(m_Data.end(), data, data + size);
		m_Pos += size;
		return true;
	}

	std::size_t const available = m_Pos <= m_Data.size() ? m_Data.size() - m_Pos : 0;
	std::size_t const copySize = std::min(size, available);
	if (copySize != 0)
		std::copy_n(m_Data.data() + m_Pos, copySize, data);
	std::fill(data + copySize, data + size, u8{0});
	m_Pos += copySize;
	return copySize == size;
}
