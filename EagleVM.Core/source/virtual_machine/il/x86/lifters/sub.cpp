#include "eaglevm-core/virtual_machine/il/x86/lifters/sub.h"

namespace eagle::il::lifter
{
    sub::sub()
    {
        entries = {
            { codec::gpr_64, 2 },
            { codec::gpr_32, 2 },
            { codec::gpr_16, 2 },
        };
    }

    il_insts sub::gen_il(codec::reg_class size, uint8_t operands)
    {
    }
}
