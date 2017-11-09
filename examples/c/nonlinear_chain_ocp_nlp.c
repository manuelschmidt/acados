/*
 *    This file is part of acados.
 *
 *    acados is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    acados is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with acados; if not, write to the Free Software Foundation,
 *    Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

// #include <string>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
// blasfeo
#include "blasfeo/include/blasfeo_target.h"
#include "blasfeo/include/blasfeo_common.h"
#include "blasfeo/include/blasfeo_d_aux_ext_dep.h"
#include "blasfeo/include/blasfeo_i_aux_ext_dep.h"
// acados
#include "acados/ocp_nlp/ocp_nlp_gn_sqp.h"
#include "acados/ocp_qp/ocp_qp_common.h"
#include "acados/sim/casadi_wrapper.h"
#include "acados/sim/sim_common.h"
#include "acados/sim/sim_erk_integrator.h"
#include "acados/sim/sim_lifted_irk_integrator.h"
#include "acados/utils/print.h"
#include "acados/utils/timing.h"
#include "acados/utils/types.h"

#include "examples/c/chain_model/chain_model.h"



#define NN 15
#define TT 3.0
#define Ns 2

// using Eigen::MatrixXd;
// using Eigen::VectorXd;

int main() {
    // TODO(dimitris): fix for NMF > 1
    const int INEXACT = 0;
    const int d = 2;
    const int NMF = 1;
    if (INEXACT == 0) {
        printf(
            "\n----- NUMBER OF FREE MASSES = %d, d = %d (Exact Newton) -----\n",
            NMF, d);
    } else if (INEXACT == 1) {
        printf("\n----- NUMBER OF FREE MASSES = %d, d = %d (IN Scheme) -----\n",
               NMF, d);
    } else if (INEXACT == 2) {
        printf(
            "\n----- NUMBER OF FREE MASSES = %d, d = %d (INIS Scheme) -----\n",
            NMF, d);
    } else if (INEXACT == 3) {
        printf(
            "\n----- NUMBER OF FREE MASSES = %d, d = %d (FROZEN IN Scheme) "
            "-----\n",
            NMF, d);
    } else if (INEXACT == 4) {
        printf(
            "\n----- NUMBER OF FREE MASSES = %d, d = %d (FROZEN INIS Scheme) "
            "-----\n",
            NMF, d);
    }
    int_t NX = 6 * NMF;
    int_t NU = 3;
    int_t jj;

    real_t wall_pos = -0.01;
    int_t UMAX = 10;

    // Problem data
    ocp_nlp_ls_cost ls_cost;
    real_t *W, *WN;
    int_t max_sqp_iters = 20;
    real_t *x_end;
    real_t *u_end;

    d_zeros(&W, NX + NU, NX + NU);
    d_zeros(&WN, NX, NX);
    d_zeros(&x_end, NX, 1);
    d_zeros(&u_end, NU, 1);

    real_t x0[6] = {0.0000000000000000e+00, 1.5000000000000000e+00,
                    5.0000000000000000e-01, 0.0000000000000000e+00,
                    0.0000000000000000e+00, 0.0000000000000000e+00};

    // real_t xref[6] = {1.0000000000000000e+00, 0.0000000000000000e+00,
    //                   0.0000000000000000e+00, 0.0000000000000000e+00,
    //                   0.0000000000000000e+00, 0.0000000000000000e+00};

    // real_t uref[3] = {0.0000000000000000e+00, 0.0000000000000000e+00, 0.0000000000000000e+00};

    // TODO(dimitris): REMOVE THOSE TEMP REF VALUES FOR DEBUGGING
    real_t xref[6] = {1.0000000000000000e+00, 2.0000000000000000e+00,
                      3.0000000000000000e+00, 4.0000000000000000e+00,
                      5.0000000000000000e+00, 6.0000000000000000e+00};

    // TODO(dimitris): why changing this has no effect on solution??
    real_t uref[3] = {1.1000000000000000e+00, 2.2000000000000000e+00, 3.3000000000000000e+00};

    for (int_t i = 0; i < NX; i++) W[i * (NX + NU + 1)] = 1e-2;
    for (int_t i = 0; i < NU; i++) W[(NX + i) * (NX + NU + 1)] = 1.0;
    for (int_t i = 0; i < NX; i++) WN[i * (NX + 1)] = 1e-2;

    ls_cost.W = (real_t **)malloc(sizeof(*ls_cost.W) * (NN + 1));
    for (int_t i = 0; i < NN; i++) ls_cost.W[i] = W;
    ls_cost.W[NN] = WN;
    ls_cost.y_ref = (real_t **)malloc(sizeof(*ls_cost.y_ref) * (NN + 1));
    for (int_t i = 0; i < NN; i++) {
        ls_cost.y_ref[i] = (real_t *)malloc(sizeof(*ls_cost.y_ref[i]) * (NX + NU));
        for (int_t j = 0; j < NX; j++) ls_cost.y_ref[i][j] = xref[j];
        for (int_t j = 0; j < NU; j++) ls_cost.y_ref[i][NX + j] = uref[j];
    }
    ls_cost.y_ref[NN] = (real_t *)malloc(sizeof(*ls_cost.y_ref[NN]) * (NX));
    for (int_t j = 0; j < NX; j++) ls_cost.y_ref[NN][j] = xref[j];

    // Integrator structs
    real_t Ts = TT / NN;
    sim_in sim_in[NN];
    sim_out sim_out[NN];
    sim_info info[NN];
    sim_solver integrators[NN];

    sim_RK_opts rk_opts[NN];
    sim_lifted_irk_memory irk_mem[NN];

    // TODO(rien): can I move this somewhere inside the integrator?
    // struct d_strmat str_mat[NN];
    // struct d_strmat str_sol[NN];

    for (jj = 0; jj < NN; jj++) {
        integrators[jj].in = &sim_in[jj];
        integrators[jj].out = &sim_out[jj];
        integrators[jj].args = &rk_opts[jj];
        if (d > 0) {
            integrators[jj].fun = &sim_lifted_irk;
            integrators[jj].mem = &irk_mem[jj];
        } else {
            integrators[jj].fun = &sim_erk;
            integrators[jj].mem = 0;
        }

        sim_in[jj].num_steps = Ns;
        sim_in[jj].step = Ts / sim_in[jj].num_steps;
        sim_in[jj].nx = NX;
        sim_in[jj].nu = NU;

        sim_in[jj].sens_forw = true;
        sim_in[jj].sens_adj = false;
        sim_in[jj].sens_hess = false;
        sim_in[jj].num_forw_sens = NX + NU;

        switch (NMF) {
            case 1:
                sim_in[jj].vde = &vde_chain_nm2;
                sim_in[jj].VDE_forw = &vde_fun;
                sim_in[jj].jac = &jac_chain_nm2;
                sim_in[jj].jac_fun = &jac_fun;
                break;
//            case 2:
//                sim_in[jj].vde = &vde_chain_nm3;
//                sim_in[jj].VDE_forw = &vde_fun;
//                sim_in[jj].jac = &jac_chain_nm3;
//                sim_in[jj].jac_fun = &jac_fun;
//                break;
//            case 3:
//                sim_in[jj].vde = &vde_chain_nm4;
//                sim_in[jj].VDE_forw = &vde_fun;
//                sim_in[jj].jac = &jac_chain_nm4;
//                sim_in[jj].jac_fun = &jac_fun;
//                break;
            default:
                break;
        }

        sim_in[jj].x = (real_t *)malloc(sizeof(*sim_in[jj].x) * (NX));
        sim_in[jj].u = (real_t *)malloc(sizeof(*sim_in[jj].u) * (NU));
        sim_in[jj].S_forw =
            (real_t *)malloc(sizeof(*sim_in[jj].S_forw) * (NX * (NX + NU)));
        for (int_t i = 0; i < NX * (NX + NU); i++) sim_in[jj].S_forw[i] = 0.0;
        for (int_t i = 0; i < NX; i++) sim_in[jj].S_forw[i * (NX + 1)] = 1.0;

        sim_in[jj].S_adj =
            (real_t *)malloc(sizeof(*sim_in[jj].S_adj) * (NX + NU));
        for (int_t i = 0; i < NX + NU; i++) sim_in[jj].S_adj[i] = 0.0;

        sim_in[jj].grad_K =
            (real_t *)malloc(sizeof(*sim_in[jj].grad_K) * (d * NX));
        for (int_t i = 0; i < d * NX; i++) sim_in[jj].grad_K[i] = 0.0;

        sim_out[jj].xn = (real_t *)malloc(sizeof(*sim_out[jj].xn) * (NX));
        sim_out[jj].S_forw =
            (real_t *)malloc(sizeof(*sim_out[jj].S_forw) * (NX * (NX + NU)));
        sim_out[jj].info = &info[jj];
        sim_out[jj].grad =
            (real_t *)malloc(sizeof(*sim_out[jj].grad) * (NX + NU));

        int_t workspace_size;
        if (d > 0) {
            sim_irk_create_arguments(&rk_opts[jj], d, "Gauss");
            if (INEXACT == 0) {
                sim_irk_create_Newton_scheme(&rk_opts[jj], d, "Gauss", exact);
            } else if (INEXACT == 1 || INEXACT == 3) {
                sim_irk_create_Newton_scheme(&rk_opts[jj], d, "Gauss",
                                             simplified_in);
            } else if (INEXACT == 2 || INEXACT == 4) {
                sim_irk_create_Newton_scheme(&rk_opts[jj], d, "Gauss",
                                             simplified_inis);
            }

            workspace_size = sim_lifted_irk_calculate_workspace_size(
                &sim_in[jj], &rk_opts[jj]);
            sim_lifted_irk_create_memory(&sim_in[jj], &rk_opts[jj],
                                         &irk_mem[jj]);
        } else {
            sim_erk_create_arguments(&rk_opts[jj], 4);
            workspace_size =
                sim_erk_calculate_workspace_size(&sim_in[jj], &rk_opts[jj]);
        }
        integrators[jj].work = (void *) malloc(workspace_size);
    }

    int_t nx[NN + 1] = {0};
    int_t nu[NN + 1] = {0};
    int_t nb[NN + 1] = {0};
    int_t nc[NN + 1] = {0};
    int_t ng[NN + 1] = {0};
    for (int_t i = 0; i < NN; i++)
    {
        nx[i] = NX;
        nu[i] = NU;
    }
    nx[NN] = NX;
    nu[NN] = 0;

    /************************************************
     * box constraints
     ************************************************/

    int *idxb0;
    int_zeros(&idxb0, NX + NU, 1);
    real_t *lb0;
    d_zeros(&lb0, NX + NU, 1);
    real_t *ub0;
    d_zeros(&ub0, NX + NU, 1);
#ifdef FLIP_BOUNDS
    for (jj = 0; jj < NU; jj++) {
        lb0[jj] = -UMAX;  // umin
        ub0[jj] = UMAX;   // umax
        idxb0[jj] = jj;
    }
    for (jj = 0; jj < NX; jj++) {
        lb0[NU+jj] = x0[jj];  // xmin
        ub0[NU+jj] = x0[jj];  // xmax
        idxb0[NU+jj] = NU+jj;
    }
#else
    for (jj = 0; jj < NX; jj++) {
        lb0[jj] = x0[jj];  // xmin
        ub0[jj] = x0[jj];  // xmax
        idxb0[jj] = jj;
    }
    for (jj = 0; jj < NU; jj++) {
        lb0[NX+jj] = -UMAX;  // umin
        ub0[NX+jj] = UMAX;   // umax
        idxb0[NX+jj] = NX+jj;
    }
#endif

    nb[0] = NX + NU;

    int *idxb1;
    int_zeros(&idxb1, NMF + NU, 1);
    double *lb1[NN - 1];
    double *ub1[NN - 1];
    for (int_t i = 0; i < NN - 1; i++) {
        d_zeros(&lb1[i], NMF + NU, 1);
        d_zeros(&ub1[i], NMF + NU, 1);
#ifdef FLIP_BOUNDS
        for (jj = 0; jj < NU; jj++) {
            lb1[i][jj] = -UMAX;  // umin
            ub1[i][jj] = UMAX;   // umax
            idxb1[jj] = jj;
        }
        for (jj = 0; jj < NMF; jj++) {
            lb1[i][NU+jj] = wall_pos;  // wall position
            ub1[i][NU+jj] = 1e12;
            idxb1[NU+jj] = NU + 6 * jj + 1;
        }
#else
        for (jj = 0; jj < NMF; jj++) {
            lb1[i][jj] = wall_pos;  // wall position
            ub1[i][jj] = 1e12;
            idxb1[jj] = 6 * jj + 1;
        }
        for (jj = 0; jj < NU; jj++) {
            lb1[i][NMF+jj] = -UMAX;  // umin
            ub1[i][NMF+jj] = UMAX;   // umax
            idxb1[NMF+jj] = NX+jj;
        }
#endif
        nb[i + 1] = NMF + NU;
    }

    int *idxbN;
    int_zeros(&idxbN, NX, 1);
    real_t *lbN;
    d_zeros(&lbN, NX, 1);
    real_t *ubN;
    d_zeros(&ubN, NX, 1);
    for (jj = 0; jj < NX; jj++) {
        lbN[jj] = xref[jj];  // xmin
        ubN[jj] = xref[jj];  // xmax
        idxbN[jj] = jj;
    }
    nb[NN] = NX;

    real_t *hlb[NN + 1];
    real_t *hub[NN + 1];
    int *hidxb[NN + 1];

    hlb[0] = lb0;
    hub[0] = ub0;
    hidxb[0] = idxb0;
    for (int_t i = 1; i < NN; i++) {
        hlb[i] = lb1[i - 1];
        hub[i] = ub1[i - 1];
        hidxb[i] = idxb1;
    }
    hlb[NN] = lbN;
    hub[NN] = ubN;
    hidxb[NN] = idxbN;

    ocp_nlp_in nlp_in;
    nlp_in.N = NN;
    nlp_in.nx = nx;
    nlp_in.nu = nu;
    nlp_in.nb = nb;
    nlp_in.nc = nc;
    nlp_in.ng = ng;
    nlp_in.idxb = (const int_t **)hidxb;
    nlp_in.lb = (const real_t **)hlb;
    nlp_in.ub = (const real_t **)hub;
    nlp_in.sim = integrators;
    nlp_in.cost = &ls_cost;
    nlp_in.freezeSens = false;
    if (INEXACT > 2) nlp_in.freezeSens = true;

    ocp_nlp_out nlp_out;
    nlp_out.x = (real_t **)malloc(sizeof(*nlp_out.x) * (NN + 1));
    nlp_out.u = (real_t **)malloc(sizeof(*nlp_out.u) * NN);
    nlp_out.lam = (real_t **)malloc(sizeof(*nlp_out.lam) * NN);
    for (int_t i = 0; i < NN; i++) {
        nlp_out.x[i] = (real_t *)malloc(sizeof(*nlp_out.x[i]) * (NX));
        nlp_out.u[i] = (real_t *)malloc(sizeof(*nlp_out.u[i]) * (NU));
        nlp_out.lam[i] = (real_t *)malloc(sizeof(*nlp_out.lam[i]) * (NX));
    }
    nlp_out.x[NN] = (real_t *)malloc(sizeof(*nlp_out.x[NN]) * (NX));


    // TODO(dimitris): clean this up !

    ocp_nlp_dims dims;
    dims.N = nlp_in.N;
    dims.nx = (int *)nlp_in.nx;
    dims.nu = (int *)nlp_in.nu;
    dims.ng = (int *)nlp_in.nc;
    dims.nb = (int *)nlp_in.nb;
    int NS[NN+1];
    int NBX[NN+1];
    int NBU[NN+1];
    for (int ii = 0; ii < NN+1; ii++) NS[ii] = 0;
    dims.ns = NS;
    form_nbu_nbx_rev(NN, NBU, NBX, dims.nb, dims.nx, dims.nu, (int **)nlp_in.idxb);
    dims.nbx = NBX;
    dims.nbu = NBU;

    /************************************************
    * gn_sqp args
    ************************************************/

    // choose QP solver and set its function pointers
    ocp_qp_solver qp_solver = initialize_ocp_qp_solver(CONDENSING_QPOASES);

    // set up args with nested structs
    ocp_nlp_gn_sqp_args *nlp_args = ocp_nlp_gn_sqp_create_args(&dims, &qp_solver);
    nlp_args->common->maxIter = max_sqp_iters;

    /************************************************
    * gn_sqp memory
    ************************************************/

    ocp_nlp_gn_sqp_memory *nlp_mem = new_ocp_nlp_gn_sqp_create_memory(&dims, &qp_solver, nlp_args);

    for (int_t i = 0; i < NN; i++) {
        for (int_t j = 0; j < NX; j++)
            nlp_mem->common->x[i][j] = xref[j];  // resX(j,i)
        for (int_t j = 0; j < NU; j++)
            nlp_mem->common->u[i][j] = uref[j];  // resU(j, i)
    }
    for (int_t j = 0; j < NX; j++)
        nlp_mem->common->x[NN][j] = xref[j];  // resX(j, NN)

    /************************************************
    * gn_sqp workspace
    ************************************************/

    int work_space_size = ocp_nlp_gn_sqp_calculate_workspace_size(&dims, &qp_solver, nlp_args);

    void *nlp_work = (void *)malloc(work_space_size);

    /************************************************
    * gn_sqp solve
    ************************************************/

    int status = ocp_nlp_gn_sqp(&nlp_in, &nlp_out, nlp_args, nlp_mem, nlp_work);

    printf("\n\nstatus = %i\n\n", status);

    for (int_t k =0; k < 3; k++) {
        printf("u[%d] = \n", k);
        d_print_mat(1, nu[k], nlp_out.u[k], 1);
        printf("x[%d] = \n", k);
        d_print_mat(1, nx[k], nlp_out.x[k], 1);
    }
    printf("u[N-1] = \n");
    d_print_mat(1, nu[NN-1], nlp_out.u[NN-1], 1);
    printf("x[N] = \n");
    d_print_mat(1, nx[NN], nlp_out.x[NN], 1);

    /************************************************
    * free memory
    ************************************************/

    d_free(W);
    d_free(WN);
    d_free(x_end);
    d_free(u_end);

    int_free(idxb0);
    d_free(lb0);
    d_free(ub0);
    int_free(idxb1);
    for (jj = 0; jj < NN - 1; jj++) {
        d_free(lb1[jj]);
        d_free(ub1[jj]);
    }
    int_free(idxbN);
    d_free(lbN);
    d_free(ubN);

    for (jj = 0; jj < NN; jj++) {
        free(sim_in[jj].x);
        free(sim_in[jj].u);
        free(sim_in[jj].S_forw);
        free(sim_in[jj].S_adj);
        free(sim_in[jj].grad_K);
        free(sim_out[jj].xn);
        free(sim_out[jj].S_forw);
        free(sim_out[jj].grad);
        free(ls_cost.y_ref[jj]);
    }
    free(ls_cost.y_ref[NN]);
    free(ls_cost.y_ref);
    free(ls_cost.W);

    for (jj = 0; jj < NN; jj++) {
        free(nlp_in.sim[jj].work);
        free(nlp_out.x[jj]);
        free(nlp_out.u[jj]);
        free(nlp_out.lam[jj]);
    }
    free(nlp_out.x[NN]);
    free(nlp_out.x);
    free(nlp_out.u);
    free(nlp_out.lam);

    free(nlp_work);
    free(nlp_mem);
    free(nlp_args);
}
