#pragma once

// Target description boundary.
//
// Core history code may ask for architecture/cpu/machine names and for
// human-readable interrupt/peripheral names, but must never contain
// SoC-specific tables itself. SoC knowledge lives in TargetDescriptor
// implementations (plugins or target packs).

#include <QString>

#include <optional>

namespace qddd {
namespace history {

class TargetDescriptor {
public:
    virtual ~TargetDescriptor() = default;

    virtual QString architecture() const = 0; // e.g. "ARM", "RISC-V"
    virtual QString cpuModel() const = 0;     // e.g. "Cortex-M4", "RV32IMAC"
    virtual QString machine() const = 0;      // e.g. board/machine name

    virtual std::optional<QString> interruptName(unsigned number) const
    {
        Q_UNUSED(number);
        return std::nullopt;
    }

    virtual std::optional<QString> peripheralName(quint64 address) const
    {
        Q_UNUSED(address);
        return std::nullopt;
    }
};

// Fallback descriptor when no target pack is installed: reports identity
// strings and no decoded names. Used so the core never hard-codes a target.
class GenericTargetDescriptor : public TargetDescriptor {
public:
    GenericTargetDescriptor(QString architecture = QStringLiteral("unknown"),
                            QString cpuModel = QStringLiteral("unknown"),
                            QString machine = QStringLiteral("unknown"))
        : m_architecture(std::move(architecture))
        , m_cpuModel(std::move(cpuModel))
        , m_machine(std::move(machine))
    {
    }

    QString architecture() const override { return m_architecture; }
    QString cpuModel() const override { return m_cpuModel; }
    QString machine() const override { return m_machine; }

private:
    QString m_architecture;
    QString m_cpuModel;
    QString m_machine;
};

// Generic ARM Cortex-M exception decoder (architecture-level, NOT SoC
// specific). System exceptions 1-15 have fixed ARM names; external
// interrupts (>= 16) are reported as "IRQ<n>" because vector naming is
// defined by the SoC and must come from an SoC-specific TargetDescriptor.
class ArmCortexMTargetDescriptor : public TargetDescriptor {
public:
    ArmCortexMTargetDescriptor(QString cpuModel = QStringLiteral("Cortex-M"),
                               QString machine = QStringLiteral("unknown"))
        : m_cpuModel(std::move(cpuModel))
        , m_machine(std::move(machine))
    {
    }

    QString architecture() const override { return QStringLiteral("ARM"); }
    QString cpuModel() const override { return m_cpuModel; }
    QString machine() const override { return m_machine; }

    std::optional<QString> interruptName(unsigned number) const override
    {
        switch (number) {
        case 1: return QStringLiteral("Reset");
        case 2: return QStringLiteral("NMI");
        case 3: return QStringLiteral("HardFault");
        case 4: return QStringLiteral("MemManage");
        case 5: return QStringLiteral("BusFault");
        case 6: return QStringLiteral("UsageFault");
        case 11: return QStringLiteral("SVCall");
        case 12: return QStringLiteral("DebugMonitor");
        case 14: return QStringLiteral("PendSV");
        case 15: return QStringLiteral("SysTick");
        default: break;
        }
        if (number >= 16)
            return QStringLiteral("IRQ%1").arg(number - 16);
        return std::nullopt;
    }

private:
    QString m_cpuModel;
    QString m_machine;
};

} // namespace history
} // namespace qddd
