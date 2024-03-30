#pragma once
#include "eaglevm-core/virtual_machine/handlers/handler/vm_handler_entry.h"

namespace eagle::virt::handle
{
    class vm_store_handler : public vm_handler_entry
    {
    public:
        vm_store_handler(vm_inst_regs* manager, vm_inst_handlers* handler_generator)
            : vm_handler_entry(manager, handler_generator)
        {
            handlers = {
                {bit64, 0},
                {bit32, 0},
                {bit16, 0},
                {bit8, 0},
            };
        };

    private:
        void construct_single(asmbl::function_container& container, reg_size size, uint8_t operands, handler_override override,
            bool inlined = false) override;
    };
}
