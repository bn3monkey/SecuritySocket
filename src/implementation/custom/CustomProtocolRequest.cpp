#include "CustomProtocolRequest.hpp"

using namespace Bn3Monkey;

CustomProtocolRequestImpl::CustomProtocolRequestImpl(
    const void* header, size_t header_length,
    const void* payload, size_t payload_length)
    : _header(header), _header_length(header_length),
      _payload(payload), _payload_length(payload_length)
{
}
