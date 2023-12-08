/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    polysat constraints

Author:

    Nikolaj Bjorner (nbjorner) 2021-03-19
    Jakob Rath 2021-04-06

--*/

#include "sat/smt/polysat_core.h"
#include "sat/smt/polysat_solver.h"
#include "sat/smt/polysat_constraints.h"
#include "sat/smt/polysat_ule.h"

namespace polysat {

    signed_constraint constraints::ule(pdd const& p, pdd const& q) {
        pdd lhs = p, rhs = q;
        bool is_positive = true;
        ule_constraint::simplify(is_positive, lhs, rhs);
        auto* c = alloc(ule_constraint, p, q);
        m_trail.push(new_obj_trail(c));
        auto sc = signed_constraint(ckind_t::ule_t, c);
        return is_positive ? sc : ~sc;
    }
}
