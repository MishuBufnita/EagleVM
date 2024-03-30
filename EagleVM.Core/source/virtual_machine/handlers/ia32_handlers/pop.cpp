#include "eaglevm-core/virtual_machine/handlers/ia32_handlers/pop.h"

namespace eagle::virt::handle
{
    void ia32_pop_handler::construct_single(asmbl::function_container& container, reg_size reg_size, uint8_t operands, handler_override override,
        bool inlined)
    {
        uint64_t size = reg_size;
        dynamic_instructions_vec handle_instructions;

        //mov VTEMP, [VSP]
        //add VSP, reg_size

        const zydis_register target_temp = zydis_helper::get_bit_version(VTEMP, reg_size);
        container.add({
            zydis_helper::enc(ZYDIS_MNEMONIC_MOV, ZREG(target_temp), ZMEMBD(VSP, 0, size)),
            zydis_helper::enc(ZYDIS_MNEMONIC_LEA, ZREG(VSP), ZMEMBD(VSP, size, 8)),
        });

        if (!inlined)
            create_vm_return(container);
    }
}
