/*++
Copyright (c) 2022 Microsoft Corporation

Module Name:

    Clause Simplification

Author:

    Jakob Rath, Nikolaj Bjorner (nbjorner) 2022-08-22

--*/
#pragma once
#include "math/polysat/constraint.h"
#include "math/polysat/forbidden_intervals.h"

namespace polysat {

    class solver;

    class simplify_clause {

        struct subs_entry : fi_record {
            optional<pdd>  var;
            bool subsumed = false;
            bool valid = false;
        };

        solver& s;
        vector<subs_entry> m_entries;

        bool try_equal_body_subsumptions(clause& cl);

        void prepare_subs_entry(subs_entry& entry, signed_constraint c);

        pdd abstract(pdd const& p, pdd& v);
        
    public:
        simplify_clause(solver& s);

        bool apply(clause& cl);
    };

}