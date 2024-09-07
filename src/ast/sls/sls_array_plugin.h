/*++
Copyright (c) 2020 Microsoft Corporation

Module Name:

    sls_array_plugin.h

Abstract:

    Theory plugin for arrays local search

Author:

    Nikolaj Bjorner (nbjorner) 2024-07-06

--*/
#pragma once

#include "ast/sls/sls_context.h"
#include "ast/array_decl_plugin.h"
#include "ast/euf/euf_egraph.h"

namespace sls {

    class array_plugin : public plugin {
        typedef obj_map<euf::enode, obj_map<euf::enode, euf::enode*>> kv;

        array_util     a;
        scoped_ptr<euf::egraph> m_g;
        scoped_ptr<kv> m_kv;
        bool m_add_conflicts = true;

        void init_egraph(euf::egraph& g);
        void init_kv(euf::egraph& g, kv& kv);
        void saturate_store(euf::egraph& g);
        void force_store_axiom1(euf::egraph& g, euf::enode* n);
        void force_store_axiom2_down(euf::egraph& g, euf::enode* sto, euf::enode* sel);
        void force_store_axiom2_up(euf::egraph& g, euf::enode* sto, euf::enode* sel);
        void add_store_axiom1(app* sto);
        void add_store_axiom2(app* sto, app* sel);
        bool are_distinct(euf::enode* a, euf::enode* b);
        bool eq_args(euf::enode* sto, euf::enode* sel);
        euf::enode* mk_select(euf::egraph& g, euf::enode* b, euf::enode* sel);
        
    public:
        array_plugin(context& ctx);
        ~array_plugin() override {}
        void register_term(expr* e) override { }
        expr_ref get_value(expr* e) override;
        void initialize() override { m_g = nullptr; }
        void propagate_literal(sat::literal lit) override { m_g = nullptr; }
        bool propagate() override { return false; }
        bool repair_down(app* e) override { return true; }
        void repair_up(app* e) override {}
        void repair_literal(sat::literal lit) override { m_g = nullptr; }
        bool is_sat() override;

        void on_rescale() override {}
        void on_restart() override {}
        std::ostream& display(std::ostream& out) const override;
        void mk_model(model& mdl) override {}
        bool set_value(expr* e, expr* v) override { return false; }
        void collect_statistics(statistics& st) const override {}
        void reset_statistics() override {}
    };

}
