#include "ParameterId.h"

namespace apollo::params
{

std::string_view describeParameterIdIssue (ParameterIdIssue issue) noexcept
{
    switch (issue)
    {
        case ParameterIdIssue::none:                  return "identifier is well-formed";
        case ParameterIdIssue::empty:                 return "identifier is empty";
        case ParameterIdIssue::tooLong:               return "identifier is longer than the permitted maximum";
        case ParameterIdIssue::invalidFirstCharacter: return "identifier must begin with a lower-case letter";
        case ParameterIdIssue::invalidCharacter:      return "identifier may only contain lower-case letters, digits and underscores";
        case ParameterIdIssue::trailingSeparator:     return "identifier must not end with an underscore";
        case ParameterIdIssue::emptySegment:          return "identifier must not contain consecutive underscores";
        case ParameterIdIssue::numericOnlySegment:    return "every identifier segment must contain at least one letter";
        case ParameterIdIssue::tooFewSegments:        return "identifier must contain a domain and a name segment";
    }

    return "identifier is not valid";
}

} // namespace apollo::params
