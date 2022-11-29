/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    polysat multiplication overflow constraint

Author:

    Jakob Rath, Nikolaj Bjorner (nbjorner) 2021-12-09

--*/
#pragma once
#include "math/polysat/constraint.h"

namespace polysat {

    class solver;

    class smul_fl_constraint final : public constraint {
        friend class constraint_manager;

        bool m_is_overflow;
        pdd  m_p;
        pdd  m_q;

        void simplify();
        smul_fl_constraint(constraint_manager& m, pdd const& p, pdd const& q, bool is_overflow);

    public:
        ~smul_fl_constraint() override {}
        bool is_overflow() const { return m_is_overflow; }
        pdd const& p() const { return m_p; }
        pdd const& q() const { return m_q; }
        std::ostream& display(std::ostream& out, lbool status) const override;
        std::ostream& display(std::ostream& out) const override;
        lbool eval() const override { return l_undef; }  // TODO
        lbool eval(assignment const& a) const override { return l_undef; }  // TODO
        void narrow(solver& s, bool is_positive, bool first) override;

        inequality as_inequality(bool is_positive) const override { throw default_exception("is not an inequality"); }
        unsigned hash() const override;
        bool operator==(constraint const& other) const override;
        bool is_eq() const override { return false; }

        void add_to_univariate_solver(solver& s, univariate_solver& us, unsigned dep, bool is_positive) const override;
    };

}
