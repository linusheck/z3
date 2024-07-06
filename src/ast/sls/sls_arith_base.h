/*++
Copyright (c) 2020 Microsoft Corporation

Module Name:

    sls_arith_base.h

Abstract:

    Theory plugin for arithmetic local search

Author:

    Nikolaj Bjorner (nbjorner) 2020-09-08

--*/
#pragma once

#include "util/obj_pair_set.h"
#include "util/checked_int64.h"
#include "ast/ast_trail.h"
#include "ast/arith_decl_plugin.h"
#include "ast/sls/sls_smt.h"

namespace sls {

    using theory_var = int;

    // local search portion for arithmetic
    template<typename num_t>
    class arith_base : public plugin {
        enum class ineq_kind { EQ, LE, LT};
        enum class var_sort { INT, REAL };
        typedef unsigned var_t;
        typedef unsigned atom_t;

        struct config {
            double cb = 0.0;
            unsigned L = 20;
            unsigned t = 45;
            unsigned max_no_improve = 500000;
            double sp = 0.0003;
        };

        struct stats {
            unsigned m_num_flips = 0;
        };

    public:
        struct linear_term {
            vector<std::pair<num_t, var_t>> m_args;
            num_t      m_coeff{ 0 };
        };
        // encode args <= bound, args = bound, args < bound
        struct ineq : public linear_term {            
            ineq_kind  m_op = ineq_kind::LE;            
            num_t      m_args_value;
            unsigned   m_var_to_flip = UINT_MAX;

            bool is_true() const {
                switch (m_op) {
                case ineq_kind::LE:
                    return m_args_value + m_coeff <= 0;
                case ineq_kind::EQ:
                    return m_args_value + m_coeff == 0;
                default:
                    return m_args_value + m_coeff < 0;
                }
            }
            std::ostream& display(std::ostream& out) const {
                bool first = true;
                for (auto const& [c, v] : m_args)
                    out << (first ? "" : " + ") << c << " * v" << v, first = false;
                if (m_coeff != 0)
                    out << " + " << m_coeff;
                switch (m_op) {
                case ineq_kind::LE:
                    return out << " <= " << 0 << "(" << m_args_value << ")";
                case ineq_kind::EQ:
                    return out << " == " << 0 << "(" << m_args_value << ")";
                default:
                    return out << " < " << 0 << "(" << m_args_value << ")";
                }
            }
        };
    private:

        struct var_info {
            var_info(expr* e, var_sort k): m_expr(e), m_sort(k) {}
            expr*        m_expr;
            num_t        m_value{ 0 };
            num_t        m_best_value{ 0 };
            var_sort     m_sort;
            arith_op_kind m_op = arith_op_kind::LAST_ARITH_OP;
            unsigned     m_def_idx = UINT_MAX;
            vector<std::pair<num_t, sat::bool_var>> m_bool_vars;
            unsigned_vector m_muls;
            unsigned_vector m_adds;
        };

        struct mul_def {
            unsigned        m_var;
            unsigned_vector m_monomial;
        };

        struct add_def : public linear_term {
            unsigned        m_var;
        };

        struct op_def {
            unsigned m_var;
            arith_op_kind m_op;
            unsigned m_arg1, m_arg2;
        };
       
        stats                        m_stats;
        config                       m_config;
        scoped_ptr_vector<ineq>      m_bool_vars;
        vector<var_info>             m_vars;
        vector<mul_def>              m_muls;
        vector<add_def>              m_adds;
        vector<op_def>               m_ops;
        unsigned_vector              m_expr2var;
        bool                         m_dscore_mode = false;
        arith_util                   a;
        unsigned_vector              m_defs_to_update;
        vector<std::pair<var_t, num_t>> m_vars_to_update;

        unsigned get_num_vars() const { return m_vars.size(); }

        void repair_mul(mul_def const& md);
        void repair_add(add_def const& ad);
        void repair_mod(op_def const& od);
        void repair_idiv(op_def const& od);
        void repair_div(op_def const& od);
        void repair_rem(op_def const& od);
        void repair_power(op_def const& od);
        void repair_abs(op_def const& od);
        void repair_to_int(op_def const& od);
        void repair_to_real(op_def const& od);
        void repair_defs_and_updates();
        void repair_defs();
        void repair_updates();
        void repair(sat::literal lit);
        void repair(sat::literal lit, ineq const& ineq);

        double reward(sat::literal lit);

        bool sign(sat::bool_var v) const { return !ctx.is_true(sat::literal(v, false)); }
        ineq* atom(sat::bool_var bv) const { return m_bool_vars.get(bv, nullptr); }

        
        num_t dtt(bool sign, ineq const& ineq) const { return dtt(sign, ineq.m_args_value, ineq); }
        num_t dtt(bool sign, num_t const& args_value, ineq const& ineq) const;
        num_t dtt(bool sign, ineq const& ineq, var_t v, num_t const& new_value) const;
        num_t dtt(bool sign, ineq const& ineq, num_t const& coeff, num_t const& old_value, num_t const& new_value) const;
        num_t dts(unsigned cl, var_t v, num_t const& new_value) const;
        num_t compute_dts(unsigned cl) const;
        bool cm(ineq const& ineq, var_t v, num_t& new_value);
        bool cm(ineq const& ineq, var_t v, num_t const& coeff, num_t& new_value);
        int cm_score(var_t v, num_t const& new_value);
        void update(var_t v, num_t const& new_value);
        double dscore_reward(sat::bool_var v);
        double dtt_reward(sat::literal lit);
        double dscore(var_t v, num_t const& new_value) const;
        void save_best_values();

        var_t mk_var(expr* e);
        var_t mk_term(expr* e);
        var_t mk_op(arith_op_kind k, expr* e, expr* x, expr* y);
        void add_arg(linear_term& term, num_t const& c, var_t v);
        void add_args(linear_term& term, expr* e, num_t const& sign);
        ineq& new_ineq(ineq_kind op, num_t const& bound);
        void init_ineq(sat::bool_var bv, ineq& i);
        num_t divide(var_t v, num_t const& delta, num_t const& coeff);
        
        void init_bool_var_assignment(sat::bool_var v);

        num_t value(var_t v) const { return m_vars[v].m_value; }
        bool is_num(expr* e, num_t& i);

        void check_ineqs();

    public:
        arith_base(context& ctx);
        ~arith_base() override {}
        void init_bool_var(sat::bool_var v) override;
        void register_term(expr* e) override;
        expr_ref get_value(expr* e) override;
        lbool check() override;
        bool is_sat() override;
        void reset() override;

        void on_rescale() override;
        void on_restart() override;
        std::ostream& display(std::ostream& out) const override;
        void mk_model(model& mdl) override;
    };


    inline std::ostream& operator<<(std::ostream& out, typename arith_base<checked_int64<true>>::ineq const& ineq) {
        return ineq.display(out);
    }

    inline std::ostream& operator<<(std::ostream& out, typename arith_base<rational>::ineq const& ineq) {
        return ineq.display(out);
    }
}