#include "slang-autos/CompilationUtils.h"

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/types/AllTypes.h"

namespace slang_autos {

using namespace slang::ast;

std::vector<PortInfo> getModulePortsFromCompilation(
    slang::ast::Compilation& compilation,
    const std::string& module_name,
    DiagnosticCollector* diagnostics,
    StrictnessMode strictness) {

    std::vector<PortInfo> ports;

    auto& root = compilation.getRoot();
    const InstanceBodySymbol* found_body = nullptr;

    // Search for the module in compilation's top instances
    for (auto* topInst : root.topInstances) {
        for (auto& member : topInst->body.members()) {
            if (auto* inst = member.as_if<InstanceSymbol>()) {
                if (inst->body.name == module_name) {
                    found_body = &inst->body;
                    break;
                }
            }
        }
        if (found_body) break;
    }

    if (!found_body) {
        if (diagnostics) {
            if (strictness == StrictnessMode::Strict) {
                diagnostics->addError("Module not found: " + module_name);
            } else {
                diagnostics->addWarning("Module not found: " + module_name);
            }
        }
        return ports;
    }

    // Extract ports from the body's port list
    for (auto* port : found_body->getPortList()) {
        PortInfo info;
        info.name = std::string(port->name);

        // Empty port name indicates a parsing failure (e.g., undefined macros)
        if (info.name.empty()) {
            if (diagnostics) {
                diagnostics->addError(
                    "Port with empty name in module '" + module_name +
                    "' (likely caused by undefined macros in port declaration). "
                    "Ensure all required macros are defined via +define+ or include files.",
                    "", 0, "port_parse");
            }
            return {};  // Return empty - caller will handle the error
        }

        if (auto* portSym = port->as_if<PortSymbol>()) {
            switch (portSym->direction) {
                case ArgumentDirection::In:
                    info.direction = "input";
                    break;
                case ArgumentDirection::Out:
                    info.direction = "output";
                    break;
                case ArgumentDirection::InOut:
                    info.direction = "inout";
                    break;
                default:
                    info.direction = "input";
                    break;
            }

            auto& type = portSym->getType();
            info.width = type.getBitWidth();

            if (type.isPackedArray()) {
                auto& packed = type.getCanonicalType().as<PackedArrayType>();
                auto range = packed.range;
                info.range_str = "[" + std::to_string(range.left) + ":" +
                                std::to_string(range.right) + "]";
            } else if (info.width > 1) {
                info.range_str = "[" + std::to_string(info.width - 1) + ":0]";
            }
        }

        ports.push_back(info);
    }

    return ports;
}

} // namespace slang_autos
