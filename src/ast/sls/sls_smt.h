/*++
Copyright (c) 2024 Microsoft Corporation

Module Name:

    smt_sls.h

Abstract:

    A Stochastic Local Search (SLS) Context.

Author:

    Nikolaj Bjorner (nbjorner) 2024-06-24
    
--*/
#pragma once

#include "util/sat_literal.h"
#include "util/sat_sls.h"
#include "ast/ast.h"
#include "model/model.h"
#include "util/scoped_ptr_vector.h"
#include "util/obj_hashtable.h"

namespace sls {

    class context;
    
    class plugin {
    protected:
        context&  ctx;
        ast_manager& m;
        family_id m_fid;
    public:
        plugin(context& c);
        virtual ~plugin() {}
        virtual family_id fid() { return m_fid; }
        virtual void register_term(expr* e) = 0;
        virtual expr_ref get_value(expr* e) = 0;
        virtual void init_bool_var(sat::bool_var v) = 0;
        virtual lbool check() = 0;
        virtual bool is_sat() = 0;
        virtual void reset() {};
        virtual void on_rescale() {};
        virtual void on_restart() {};
        virtual std::ostream& display(std::ostream& out) const = 0;
        virtual void mk_model(model& mdl) = 0;
    };

    using clause = std::initializer_list <sat::literal>;

    class sat_solver_context {
    public:
        virtual vector<sat::clause_info> const& clauses() const = 0;
        virtual sat::clause_info const& get_clause(unsigned idx) const = 0;
        virtual std::initializer_list<unsigned> get_use_list(sat::literal lit) = 0;
        virtual void flip(sat::bool_var v) = 0;
        virtual double reward(sat::bool_var v) = 0;
        virtual double get_weigth(unsigned clause_idx) = 0;
        virtual bool is_true(sat::literal lit) = 0;
        virtual unsigned num_vars() const = 0;
        virtual indexed_uint_set const& unsat() const = 0;
        virtual void on_model(model_ref& mdl) = 0;
        virtual sat::bool_var add_var() = 0;
        virtual void add_clause(unsigned n, sat::literal const* lits) = 0;
    };
    
    class context {
        ast_manager& m;
        sat_solver_context& s;
        scoped_ptr_vector<plugin> m_plugins;
        indexed_uint_set m_relevant, m_visited;
        expr_ref_vector m_atoms;
        unsigned_vector m_atom2bool_var;
        vector<ptr_vector<expr>> m_parents;
        sat::literal_vector m_root_literals;
        random_gen m_rand;
        bool m_initialized = false;
        bool m_new_constraint = false;
        expr_ref_vector m_subterms;

        void register_plugin(plugin* p);

        void init();
        void init_bool_var(sat::bool_var v);
        void register_terms();
        ptr_vector<expr> m_todo;
        void register_subterms(expr* e);
        void register_term(expr* e);
        sat::bool_var mk_atom(expr* e);
        
    public:
        context(ast_manager& m, sat_solver_context& s);

        // Between SAT/SMT solver and context.
        void register_atom(sat::bool_var v, expr* e);
        void reset();
        lbool check();       

        // expose sat_solver to plugins
        vector<sat::clause_info> const& clauses() const { return s.clauses(); }
        sat::clause_info const& get_clause(unsigned idx) const { return s.get_clause(idx); }
        std::initializer_list<unsigned> get_use_list(sat::literal lit) { return s.get_use_list(lit); }
        double get_weight(unsigned clause_idx) { return s.get_weigth(clause_idx); }
        unsigned num_bool_vars() const { return s.num_vars(); }
        bool is_true(sat::literal lit) { return s.is_true(lit); }  
        expr* atom(sat::bool_var v) { return m_atoms.get(v, nullptr); }
        void flip(sat::bool_var v) { s.flip(v); }
        double reward(sat::bool_var v) { return s.reward(v); }
        indexed_uint_set const& unsat() const { return s.unsat(); }
        unsigned rand() { return m_rand(); }
        sat::literal_vector const& root_literals() const { return m_root_literals; }

        void reinit_relevant();

        // Between plugin solvers
        expr_ref get_value(expr* e);
        void set_value(expr* e, expr* v);
        bool is_relevant(expr* e);  
        void add_constraint(expr* e);
        ast_manager& get_manager() { return m; }
        std::ostream& display(std::ostream& out) const;
    };
}