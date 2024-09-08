/*++
Copyright (c) 2024 Microsoft Corporation

Module Name:

    smt_sls.cpp

Abstract:

    A Stochastic Local Search (SLS) Context.

Author:

    Nikolaj Bjorner (nbjorner) 2024-06-24
    
--*/

#include "ast/sls/sls_context.h"
#include "ast/sls/sls_euf_plugin.h"
#include "ast/sls/sls_arith_plugin.h"
#include "ast/sls/sls_array_plugin.h"
#include "ast/sls/sls_bv_plugin.h"
#include "ast/sls/sls_basic_plugin.h"
#include "ast/sls/sls_model_value_plugin.h"
#include "ast/sls/sls_user_sort_plugin.h"
#include "ast/ast_ll_pp.h"
#include "ast/ast_pp.h"
#include "smt/params/smt_params_helper.hpp"

namespace sls {

    plugin::plugin(context& c): 
        ctx(c), 
        m(c.get_manager()) {
    }

    context::context(ast_manager& m, sat_solver_context& s) : 
        m(m), s(s), m_atoms(m), m_allterms(m),
        m_gd(*this),
        m_ld(*this),
        m_repair_down(m.get_num_asts(), m_gd),
        m_repair_up(m.get_num_asts(), m_ld),
        m_todo(m) {
        register_plugin(alloc(euf_plugin, *this));
        register_plugin(alloc(arith_plugin, *this));
        register_plugin(alloc(bv_plugin, *this));
        register_plugin(alloc(basic_plugin, *this));
        register_plugin(alloc(array_plugin, *this));
        register_plugin(alloc(user_sort_plugin, *this));
        register_plugin(alloc(model_value_plugin, *this));
    }

    void context::updt_params(params_ref const& p) {
        smt_params_helper smtp(p);
        m_rand.set_seed(smtp.random_seed());
    }

    void context::register_plugin(plugin* p) {
        m_plugins.reserve(p->fid() + 1);
        m_plugins.set(p->fid(), p);
    }

    void context::register_atom(sat::bool_var v, expr* e) { 
        m_atoms.setx(v, e);
        m_atom2bool_var.setx(e->get_id(), v, sat::null_bool_var);
    }

    void context::on_restart() {
        for (auto p : m_plugins)
            if (p)
                p->on_restart();
    }
    
    lbool context::check() {
        //
        // initialize data-structures if not done before.
        // identify minimal feasible assignment to literals.
        // sub-expressions within assignment are relevant. 
        // Use timestamps to make it incremental.
        // 
        init();
        //verbose_stream() << "check " << unsat().size() << "\n";
        while (unsat().empty() && m.inc()) {

            propagate_boolean_assignment();


            // verbose_stream() << "propagate " << unsat().size() << " " << m_new_constraint << "\n";


            // display(verbose_stream());

            if (m_new_constraint || !unsat().empty())
                return l_undef;

            if (all_of(m_plugins, [&](auto* p) { return !p || p->is_sat(); })) {
                model_ref mdl = alloc(model, m);
                for (expr* e : subterms()) 
                    if (is_uninterp_const(e))
                        mdl->register_decl(to_app(e)->get_decl(), get_value(e));                
                for (auto p : m_plugins)
                    if (p)
                        p->mk_model(*mdl);
                s.on_model(mdl);
                // verbose_stream() << *mdl << "\n";
                TRACE("sls", display(tout));
                return l_true;
            }
        }
        return l_undef;
    }
    
    void context::propagate_boolean_assignment() {
        reinit_relevant();

        for (auto p : m_plugins)
            if (p)
                p->start_propagation();

        for (sat::literal lit : root_literals()) 
            propagate_literal(lit);

        if (m_new_constraint)
            return;
        

        while (!m_new_constraint && m.inc() && (!m_repair_up.empty() || !m_repair_down.empty())) {
            while (!m_repair_down.empty() && !m_new_constraint && m.inc()) {
                auto id = m_repair_down.erase_min();
                expr* e = term(id);
                TRACE("sls", tout << "repair down " << mk_bounded_pp(e, m) << "\n");
                if (is_app(e)) {
                    auto p = m_plugins.get(get_fid(e), nullptr);
                    ++m_stats.m_num_repair_down;
                    if (p && !p->repair_down(to_app(e)) && !m_repair_up.contains(e->get_id())) {
                        IF_VERBOSE(3, verbose_stream() << "revert repair: " << mk_bounded_pp(e, m) << "\n");
                        m_repair_up.insert(e->get_id());
                    }
                }
            }
            while (!m_repair_up.empty() && !m_new_constraint && m.inc()) {
                auto id = m_repair_up.erase_min();
                expr* e = term(id);
                ++m_stats.m_num_repair_up;
                TRACE("sls", tout << "repair up " << mk_bounded_pp(e, m) << "\n");
                if (is_app(e)) {
                    auto p = m_plugins.get(get_fid(e), nullptr);
                    if (p)
                        p->repair_up(to_app(e));
                }
            }
        }

        repair_literals();

        // propagate "final checks"
        bool propagated = true;
        while (propagated && !m_new_constraint) {
            propagated = false;
            for (auto p : m_plugins)
                propagated |= p && !m_new_constraint && p->propagate();
        }     

    }

    void context::repair_literals() {
        for (sat::bool_var v = 0; v < s.num_vars() && !m_new_constraint; ++v) {
            auto a = atom(v);
            if (!a)
                continue;
            sat::literal lit(v, !is_true(v));
            auto p = m_plugins.get(get_fid(a), nullptr);
            if (p)
                p->repair_literal(lit);
        }
    }

    family_id context::get_fid(expr* e) const {
        if (!is_app(e))
            return null_family_id;
        family_id fid = to_app(e)->get_family_id();
        if (m.is_eq(e))
            fid = to_app(e)->get_arg(0)->get_sort()->get_family_id();   
        return fid;
    }

    void context::propagate_literal(sat::literal lit) {
        if (!is_true(lit))
            return;
        auto a = atom(lit.var());
        if (!a)
            return;
        family_id fid = get_fid(a);
        auto p = m_plugins.get(fid, nullptr);
        if (p)
            p->propagate_literal(lit);
    }

    bool context::is_true(expr* e) {
        SASSERT(m.is_bool(e));
        auto v = m_atom2bool_var.get(e->get_id(), sat::null_bool_var);
        if (v != sat::null_bool_var)
            return m.is_true(m_plugins[basic_family_id]->get_value(e));
        else
            return is_true(v);
    }

    bool context::is_fixed(expr* e) {
        // is this a Boolean literal that is a unit?
        return false;
    }

    expr_ref context::get_value(expr* e) {
        sort* s = e->get_sort();
        auto fid = s->get_family_id();
        auto p = m_plugins.get(fid, nullptr);
        if (p) 
            return p->get_value(e);      
        verbose_stream() << fid << " " << m.get_family_name(fid) << " " << mk_pp(e, m) << "\n";
        UNREACHABLE();
        return expr_ref(e, m);
    }

    bool context::set_value(expr * e, expr * v) {
        for (auto p : m_plugins)
            if (p && p->set_value(e, v))
                return true;
        return false;
    }
    
    bool context::is_relevant(expr* e) {
        unsigned id = e->get_id();
        if (m_relevant.contains(id))
            return true;
        if (m_visited.contains(id))
            return false;
        m_visited.insert(id);
        if (m_parents.size() <= id)
            verbose_stream() << "not in map " << mk_bounded_pp(e, m) << "\n";
        for (auto p : m_parents[id]) {
            if (is_relevant(p)) {
                m_relevant.insert(id);
                return true;
            }
        }
        return false;
    }

    void context::add_constraint(expr* e) {        
        add_clause(e);        
        m_new_constraint = true;
        ++m_stats.m_num_constraints;
    }

    void context::add_clause(expr* f)  {
        expr_ref _e(f, m);
        expr* g, * h, * k;
        sat::literal_vector clause;
        if (m.is_not(f, g) && m.is_not(g, g)) {
            add_clause(g);
            return;
        }
        bool sign = m.is_not(f, f);
        if (!sign && m.is_or(f)) {
            clause.reset();
            for (auto arg : *to_app(f))
                clause.push_back(mk_literal(arg));
            s.add_clause(clause.size(), clause.data());
        }
        else if (!sign && m.is_and(f)) {
            for (auto arg : *to_app(f))
                add_clause(arg);
        }
        else if (sign && m.is_or(f)) {
            for (auto arg : *to_app(f)) {
                expr_ref fml(m.mk_not(arg), m);;
                add_clause(fml);
            }
        }
        else if (sign && m.is_and(f)) {
            clause.reset();
            for (auto arg : *to_app(f))
                clause.push_back(~mk_literal(arg));
            s.add_clause(clause.size(), clause.data());
        }
        else if (m.is_iff(f, g, h)) {
            auto lit1 = mk_literal(g);
            auto lit2 = mk_literal(h);
            sat::literal cls1[2] = { sign ? lit1 : ~lit1, lit2 };
            sat::literal cls2[2] = { sign ? ~lit1 : lit1, ~lit2 };
            s.add_clause(2, cls1);
            s.add_clause(2, cls2);
        }
        else if (m.is_ite(f, g, h, k)) {
            auto lit1 = mk_literal(g);
            auto lit2 = mk_literal(h);
            auto lit3 = mk_literal(k);
            // (g -> h) & (~g -> k)
            // (g & h) | (~g & k)
            // negated: (g -> ~h) & (g -> ~k)
            sat::literal cls1[2] = { ~lit1, sign ? ~lit2 : lit2 };
            sat::literal cls2[2] = { lit1, sign ? ~lit3 : lit3 };
            s.add_clause(2, cls1);
            s.add_clause(2, cls2);
        }
        else {
            sat::literal lit = mk_literal(f);
            if (sign)
                lit.neg();
            s.add_clause(1, &lit);
        }
    }

    void context::add_clause(sat::literal_vector const& lits) {
        //verbose_stream() << lits << "\n";
        s.add_clause(lits.size(), lits.data());
        m_new_constraint = true;
        ++m_stats.m_num_constraints;
    }

    sat::literal context::mk_literal() {
        sat::bool_var v = s.add_var();
        return sat::literal(v, false);
    }

    sat::literal context::mk_literal(expr* e) {
        expr_ref _e(e, m);
        sat::literal lit;
        bool neg = false;
        expr* a, * b, * c;
        while (m.is_not(e, e))
            neg = !neg;
        auto v = m_atom2bool_var.get(e->get_id(), sat::null_bool_var);
        if (v != sat::null_bool_var) 
            return sat::literal(v, neg);
        sat::literal_vector clause;
        lit = mk_literal();
        register_atom(lit.var(), e);
        if (m.is_true(e)) {
            clause.push_back(lit);
            s.add_clause(clause.size(), clause.data());
        }
        else if (m.is_false(e)) {
            clause.push_back(~lit);
            s.add_clause(clause.size(), clause.data());
        }
        else if (m.is_and(e)) {
            for (expr* arg : *to_app(e)) {
                auto lit2 = mk_literal(arg);
                clause.push_back(~lit2);
                sat::literal lits[2] = { ~lit, lit2 };
                s.add_clause(2, lits);
            }
            clause.push_back(lit);
            s.add_clause(clause.size(), clause.data());
        }
        else if (m.is_or(e)) {
            for (expr* arg : *to_app(e)) {
                auto lit2 = mk_literal(arg);
                clause.push_back(lit2);
                sat::literal lits[2] = { lit, ~lit2 };
                s.add_clause(2, lits);
            }
            clause.push_back(~lit);
            s.add_clause(clause.size(), clause.data());
        }
        else if (m.is_iff(e, a, b) || m.is_xor(e, a, b)) {
            auto lit1 = mk_literal(a);
            auto lit2 = mk_literal(b);
            if (m.is_xor(e))
                lit2.neg();
            sat::literal cls1[3] = { ~lit,  ~lit1, lit2 };
            sat::literal cls2[3] = { ~lit,  lit1, ~lit2 };
            sat::literal cls3[3] = { lit,  lit1, lit2 };
            sat::literal cls4[3] = { lit, ~lit1, ~lit2 };
            s.add_clause(3, cls1);
            s.add_clause(3, cls2);
            s.add_clause(3, cls3);
            s.add_clause(3, cls4);
        }
        else if (m.is_ite(e, a, b, c)) {
            auto lit1 = mk_literal(a);
            auto lit2 = mk_literal(b);
            auto lit3 = mk_literal(c);
            sat::literal cls1[3] = { ~lit, ~lit1, lit2 };
            sat::literal cls2[3] = { ~lit,  lit1, lit3 };
            sat::literal cls3[3] = { lit, ~lit1, ~lit2 };
            sat::literal cls4[3] = { lit,  lit1, ~lit3 };
            s.add_clause(3, cls1);
            s.add_clause(3, cls2);
            s.add_clause(3, cls3);
            s.add_clause(3, cls4);
        }           
        else 
            register_terms(e);                    

        return neg ? ~lit : lit;
    }


    void context::init() {
        m_new_constraint = false;
        if (m_initialized)
            return;
        m_initialized = true;
        m_unit_literals.reset();
        m_unit_indices.reset();
        for (auto const& clause : s.clauses())
            if (clause.m_clause.size() == 1) 
                m_unit_literals.push_back(clause.m_clause[0]);
        for (sat::literal lit : m_unit_literals)
            m_unit_indices.insert(lit.index());
            
        verbose_stream() << "UNITS " << m_unit_literals << "\n";
        for (auto a : m_atoms)
            if (a)
                register_terms(a);
        for (auto p : m_plugins)
            if (p)
                p->initialize();
    }

    void context::register_terms(expr* e) {
        auto is_visited = [&](expr* e) {
            return nullptr != m_allterms.get(e->get_id(), nullptr);
        };
        
        auto visit = [&](expr* e) {
            m_allterms.setx(e->get_id(), e);
        };
        if (is_visited(e))
            return;
        m_subterms.reset();
        m_todo.push_back(e);
        if (m_todo.size() > 1)
            return;
        while (!m_todo.empty()) {
            expr* e = m_todo.back();
            if (is_visited(e)) 
                m_todo.pop_back();            
            else if (is_app(e)) {
                if (all_of(*to_app(e), [&](expr* arg) { return is_visited(arg); })) {
                    expr_ref _e(e, m);
                    m_todo.pop_back();
                    m_parents.reserve(to_app(e)->get_id() + 1);
                    for (expr* arg : *to_app(e)) {
                        m_parents.reserve(arg->get_id() + 1);
                        m_parents[arg->get_id()].push_back(e);
                    }
                    if (m.is_bool(e))
                        mk_literal(e);
                    register_term(e);
                    visit(e);
                }
                else {
                    for (expr* arg : *to_app(e)) 
                        m_todo.push_back(arg);                    
                }
            }
            else {
                expr_ref _e(e, m);
                m_todo.pop_back();
                register_term(e);
                visit(e);
            }
        }
    }

    void context::new_value_eh(expr* e) {
        DEBUG_CODE(
            if (m.is_bool(e)) {
                auto v = m_atom2bool_var.get(e->get_id(), sat::null_bool_var);
                if (v != sat::null_bool_var) {
                    SASSERT(m.is_true(get_value(e)) == is_true(v));
                }                    
            }
        );

        m_repair_down.reserve(e->get_id() + 1);
        m_repair_up.reserve(e->get_id() + 1);
        if (!term(e->get_id()))
            verbose_stream() << "no term " << mk_bounded_pp(e, m) << "\n";
        SASSERT(e == term(e->get_id()));
        if (!m_repair_down.contains(e->get_id()))
            m_repair_down.insert(e->get_id());
        for (auto p : parents(e)) {
            m_repair_up.reserve(p->get_id() + 1);
            m_repair_down.reserve(p->get_id() + 1);
            if (!m_repair_up.contains(p->get_id()))
                m_repair_up.insert(p->get_id());
        }
    }

    void context::register_term(expr* e) {
        for (auto p : m_plugins)
            if (p)
                p->register_term(e);
    }

    ptr_vector<expr> const& context::subterms() {
        if (!m_subterms.empty())
            return m_subterms;
        for (auto e : m_allterms)
            if (e)
                m_subterms.push_back(e);
        std::stable_sort(m_subterms.begin(), m_subterms.end(), 
            [](expr* a, expr* b) { return get_depth(a) < get_depth(b); });        
        return m_subterms;
    }

    void context::reinit_relevant() {
        m_relevant.reset();
        m_visited.reset();
        m_root_literals.reset();

        for (auto const& clause : s.clauses()) {
            bool has_relevant = false;
            unsigned n = 0;
            sat::literal selected_lit = sat::null_literal;
            for (auto lit : clause) {
                auto atm = m_atoms.get(lit.var(), nullptr);
                if (!atm)
                    continue;
                auto a = atm->get_id();
                if (!is_true(lit))
                    continue;
                if (m_relevant.contains(a)) {
                    has_relevant = true;
                    break;
                }
                if (m_rand() % ++n == 0)
                    selected_lit = lit;
            }               
            if (!has_relevant && selected_lit != sat::null_literal) {
                m_relevant.insert(m_atoms[selected_lit.var()]->get_id());
                m_root_literals.push_back(selected_lit);
            }
        }
        shuffle(m_root_literals.size(), m_root_literals.data(), m_rand);
    }

    std::ostream& context::display(std::ostream& out) const {
        for (auto id : m_repair_down)
            out << "d " << mk_bounded_pp(term(id), m) << "\n";
        for (auto id : m_repair_up)
            out << "u " << mk_bounded_pp(term(id), m) << "\n";
        for (unsigned v = 0; v < m_atoms.size(); ++v) {
            auto e = m_atoms[v];
            if (e)
                out << v << ": " << mk_bounded_pp(e, m) << " := " << (is_true(v)?"T":"F") << "\n";

        }
        for (auto p : m_plugins) 
            if (p)
                p->display(out);
        
        return out;
    }

    void context::collect_statistics(statistics& st) const {
        for (auto p : m_plugins)
            if (p)
                p->collect_statistics(st);
        st.update("sls-repair-down", m_stats.m_num_repair_down);
        st.update("sls-repair-up", m_stats.m_num_repair_up);
        st.update("sls-constraints", m_stats.m_num_constraints);
    }

    void context::reset_statistics() {
        for (auto p : m_plugins)
            if (p)
                p->reset_statistics();
        m_stats.reset();
    }
}
