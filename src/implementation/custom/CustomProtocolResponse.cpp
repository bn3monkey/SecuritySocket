#include "CustomProtocolResponse.hpp"

using namespace Bn3Monkey;

CustomProtocolResponseImpl::CustomProtocolResponseImpl(
    void* buffer, size_t capacity, size_t* out_length)
    : _buffer(buffer), _capacity(capacity), _out_length(out_length)
{
}
