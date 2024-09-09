/*++
Copyright (c) 2024 Microsoft Corporation

Module Name:

    sls_euf_plugin.h

Abstract:

    Congruence Closure for SLS
    
Author:

    Nikolaj Bjorner (nbjorner) 2024-06-24
    
--*/
#pragma once

#include "util/hashtable.h"
#include "ast/sls/sls_context.h"
#include "ast/euf/euf_egraph.h"

namespace sls {
    
    class euf_plugin : public plugin {
        obj_map<func_decl, ptr_vector<app>> m_app;
        struct value_hash {
            euf_plugin& cc;
            value_hash(euf_plugin& cc) : cc(cc) {}
            unsigned operator()(app* t) const;
        };
        struct value_eq {
            euf_plugin& cc;
            value_eq(euf_plugin& cc) : cc(cc) {}
            bool operator()(app* a, app* b) const;
        };
        hashtable<app*, value_hash, value_eq> m_values;

        scoped_ptr<euf::egraph> m_g;
        scoped_ptr<obj_map<sort, unsigned>> m_num_elems;
        scoped_ptr<obj_map<euf::enode, expr*>> m_root2value;
        scoped_ptr<expr_ref_vector> m_pinned;

        void init_egraph(euf::egraph& g);
        bool is_user_sort(sort* s) { return s->get_family_id() == user_sort_family_id; }

        size_t* to_ptr(sat::literal l) { return reinterpret_cast<size_t*>((size_t)(l.index() << 4)); };
        sat::literal to_literal(size_t* p) { return sat::to_literal(static_cast<unsigned>(reinterpret_cast<size_t>(p) >> 4)); };

    public:
        euf_plugin(context& c);
        ~euf_plugin() override;
        family_id fid() { return m_fid; }
        expr_ref get_value(expr* e) override;
        void initialize() override {}
        void start_propagation() override;
        void propagate_literal(sat::literal lit) override;
        bool propagate() override;       
        bool is_sat() override;
        void register_term(expr* e) override;
        std::ostream& display(std::ostream& out) const override;
        void mk_model(model& mdl) override;
        bool set_value(expr* e, expr* v) override { return false; }

        void repair_up(app* e) override {}
        bool repair_down(app* e) override { return false; }
        void repair_literal(sat::literal lit) override {}

        void collect_statistics(statistics& st) const override {}
        void reset_statistics() override {}
    };
    
}