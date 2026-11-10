#include "Type.hpp"

namespace mpp::semantic {

const char* typeName(TypeKind kind) {
	switch (kind) { case TypeKind::Void: return "void"; case TypeKind::Number: return "number"; case TypeKind::Boolean: return "boolean"; case TypeKind::String: return "string"; default: return "unknown"; }
}

}
