# CSR union redesign

## Context

The CSR types in `RiscvEmulator/include/RiscvEmulatorTypeCSR*.h` are packed
bitfield structs. The CSR read and write path casts the struct address to
`uint32_t *`, and the WARL masks rebuild raw values with shifts. The `sstatus`
write is the clearest example: it shifts `mie`, `mpie`, `mpp`, `mprv`, `tvm`,
`tw` and `tsr` back into place.

This is bit magic. The shifts duplicate the layout that the bitfield struct
already defines, and a missing or wrong shift is not caught by the compiler.
The `uint32_t *` cast also relies on type-punning, which raises
`-Werror=strict-aliasing` in the Linux environments.

## Goal

Every CSR type is a union of an anonymous bitfield struct and a raw
`uint32_t`. The bitfields and the raw value share storage, so the code can use
the named bits and the raw value without shifts or type-punning.

## Design

```c
typedef union {
    struct __attribute__((packed)) {
        uint8_t : 1;
        uint8_t sie : 1;
        /* ... the existing fields ... */
        uint8_t tsr : 1;
        uint8_t : 8;
        uint8_t sd : 1;
    };
    uint32_t raw;
} RiscvCSRmstatus_t;
```

The anonymous struct keeps the existing `state->csr.mstatus.mie` accesses
working. The raw member is `state->csr.mstatus.raw`. The `sstatus` write
becomes:

```c
value = (state->csr.mstatus.raw & ~RVE_CSR_SSTATUS_WRITE_MASK) |
        (value & RVE_CSR_SSTATUS_WRITE_MASK);
```

The CSR read and write path can use `.raw` for the raw value. The generic
`RiscvEmulatorCSR()` still receives a `void *` and casts it to `uint32_t *`;
the union's first member is the anonymous struct, and the raw member is at
offset zero, so the cast is valid.

## Scope

- `RiscvEmulatorTypeCSRMachineInformationRegister.h`
- `RiscvEmulatorTypeCSRMachineMemoryProtection.h`
- `RiscvEmulatorTypeCSRMachineNonMaskableInterruptHandling.h`
- `RiscvEmulatorTypeCSRMachineTrapHandling.h`
- `RiscvEmulatorTypeCSRMachineTrapSetup.h`
- `RiscvEmulatorTypeCSRSupervisorProtectionAndTranslation.h`

The 64-bit counters (`cycle`, `time`, `instret`, `mhpmcounter3`) stay
`uint64_t`; they are already raw.

## Risks

- The bitfield layout is compiler-dependent. The current structs already rely
  on it, so the union makes the assumption explicit instead of adding a new
  one.
- Anonymous structs and unions are C11. The project builds with C11 or later.
- The `packed` attribute must stay on the anonymous struct so the layout does
  not change.

## Validation

- Build every PlatformIO environment, including the Linux environments.
- Run the full ACT matrix; the non-Sv32 environments must stay at zero
  failures and the Sv32 environments at the M3 baseline.
- Run the Devices unit tests.

## Status

Parked. The M3 SBI and S-mode interrupt work comes first.
