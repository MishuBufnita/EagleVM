#include "eaglevm-core/virtual_machine/handlers/ia32_handlers/lea.h"

namespace eagle::virt::handle
{
    void ia32_lea_handler::construct_single(asmbl::function_container& container, reg_size size, uint8_t operands, handler_override override, bool inlined)
    {
        // value we want to move should be located at the top of the stack
        // the address we want to move TO should be located right below it

        const inst_handler_entry* pop_handler = hg_->inst_handlers[ZYDIS_MNEMONIC_POP];

        // pop                  ; load the value into VTEMP
        // mov VTEMP2, [VSP]    ; now the address at VSP is the address we want to write to
        // mov [VTEMP2], VTEMP  ; write VTEMP into the address we have
        // pop                  ; we want to maintain the stack
        call_vm_handler(container, pop_handler->get_handler_va(size, 1));
        container.add(zydis_helper::enc(ZYDIS_MNEMONIC_MOV, ZREG(VTEMP2), ZMEMBD(VSP, 0, 8)));
        container.add(zydis_helper::enc(ZYDIS_MNEMONIC_MOV, ZMEMBD(VTEMP2, 0, 8), ZREG(VTEMP)));
        call_vm_handler(container, pop_handler->get_handler_va(bit64, 1));

        if (!inlined)
            create_vm_return(container);
    }

    int ia32_lea_handler::get_op_action(const zydis_decode& inst, zyids_operand_t op_type, int index)
    {
        return vm_op_action::action_address;
    }
}
