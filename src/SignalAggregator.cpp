#include "slang-autos/SignalAggregator.h"

#include <algorithm>

namespace slang_autos {

// ============================================================================
// SignalAggregator Implementation
// ============================================================================

void SignalAggregator::addFromInstance(
    const std::string& inst_name,
    const std::vector<PortConnection>& connections,
    const std::vector<PortInfo>& ports) {

    for (const auto& conn : connections) {
        // Skip unconnected and constant ports
        if (conn.is_unconnected || conn.is_constant) {
            continue;
        }

        // Find the port info to get width
        auto port_it = std::find_if(ports.begin(), ports.end(),
            [&](const PortInfo& p) { return p.name == conn.port_name; });

        if (port_it == ports.end()) {
            continue;
        }

        const std::string& net_name = conn.signal_expr;
        int port_width = port_it->width;
        // Prefer original syntax, fallback to resolved range
        std::string port_range = port_it->getRangeStr(true);

        // Get or create net usage entry
        auto& usage = nets_[net_name];
        if (usage.info.name.empty()) {
            usage.info.name = net_name;
            usage.info.width = port_width;
            usage.info.original_range_str = port_range;
            if (port_width > 1) {
                usage.info.msb = port_width - 1;
                usage.info.lsb = 0;
            }
        } else {
            // Merge - take max width, keep original range from widest
            usage.info.merge(port_width, port_range);
        }

        // Track instance source
        if (std::find(usage.source_instances.begin(), usage.source_instances.end(), inst_name)
            == usage.source_instances.end()) {
            usage.source_instances.push_back(inst_name);
        }

        // Track direction usage
        if (conn.direction == "output") {
            usage.driven_by_instance = true;
        } else if (conn.direction == "input") {
            usage.consumed_by_instance = true;
        } else if (conn.direction == "inout") {
            usage.driven_by_instance = true;
            usage.consumed_by_instance = true;
            inout_nets_.insert(net_name);
        }
    }
}

std::vector<NetInfo> SignalAggregator::getExternalInputNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // External input: consumed by instance input but NOT driven by any instance output
        if (usage.consumed_by_instance && !usage.driven_by_instance) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getExternalOutputNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // External output: driven by instance output but NOT consumed by any instance input
        // Also exclude inouts
        if (usage.driven_by_instance && !usage.consumed_by_instance &&
            inout_nets_.find(name) == inout_nets_.end()) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getInoutNets() const {
    std::vector<NetInfo> result;
    for (const auto& net_name : inout_nets_) {
        auto it = nets_.find(net_name);
        if (it != nets_.end()) {
            result.push_back(it->second.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getInternalNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // Internal net: driven by instance output AND consumed by instance input
        // Excludes inouts (those are external bidirectional ports)
        if (usage.driven_by_instance && usage.consumed_by_instance &&
            inout_nets_.find(name) == inout_nets_.end()) {
            result.push_back(usage.info);
        }
    }
    return result;
}

const NetInfo* SignalAggregator::getNetInfo(const std::string& name) const {
    auto it = nets_.find(name);
    if (it != nets_.end()) {
        return &it->second.info;
    }
    return nullptr;
}

void SignalAggregator::addUnusedSignal(const std::string& name, int width) {
    unused_signals_.emplace_back(name, width);
}

const std::vector<NetInfo>& SignalAggregator::getUnusedSignals() const {
    return unused_signals_;
}

} // namespace slang_autos
