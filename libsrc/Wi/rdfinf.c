/*
 *  rdfinf.c
 *
 *  $Id$
 *
 *  RDF Inference
 *
 *  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
 *  project.
 *
 *  Copyright (C) 1998-2006 OpenLink Software
 *
 *  This project is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; only version 2 of the License, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "sqlnode.h"
#include "sqlbif.h"
#include "eqlcomp.h"
#include "sqlfn.h"
#include "sqlpar.h"
#include "sqlpfn.h"
#include "sqlcmps.h"
#include "sqlintrp.h"
#include "sqlo.h"
#include "list2.h"
#include "xmlnode.h"
#include "xmltree.h"
#include "arith.h"
#include "rdfinf.h"


dk_set_t
ri_list (rdf_inf_pre_node_t * ri, caddr_t iri)
{
  rdf_sub_t ** place = (rdf_sub_t **) id_hash_get (ri->ri_ctx->ric_iri_to_sub, (caddr_t)&iri);
  if (place)
    {
      rdf_sub_t * sub = *place;
      switch (ri->ri_mode)
	{
	case RI_SUBCLASS: return sub->rs_subclasses;
	case RI_SUPERCLASS: return sub->rs_superclasses;
	case RI_SUBPROPERTY: return sub->rs_subproperties;
	case RI_SUPERPROPERTY: return sub->rs_superproperties;
	}
    }
  return NULL;
}


caddr_t rdfs_type;

#define IS_HS(qn) (((data_source_t *)qn)->src_input == (qn_input_fn)hash_source_input)


void
ri_outer_output (rdf_inf_pre_node_t * ri, caddr_t * inst)
{
  data_source_t * qn;
  data_source_t * next_qn = NULL;
  table_source_t * ts = NULL;
  hash_source_t * hs = NULL;
  if (!ri->ri_outer_any_passed || qst_get (inst, ri->ri_outer_any_passed))
    return;
  /* the ts or hs after ri is outer.  Must call the appropriate outer output.  */
  while ((qn = qn_next ((data_source_t *) ri)))
    {
      if (IS_TS ((table_source_t*)qn) || IS_HS ((table_source_t*)qn))
	{
	  ts = (table_source_t *)qn;
	  next_qn = qn_next (qn_next ((data_source_t *)ts));
	  break;
	}
    }
  
  if (!next_qn)
    return;
  if (IS_TS (ts))
    {
      DO_SET (state_slot_t *, sl, &ts->ts_order_ks->ks_out_slots)
	{
	  qst_set_bin_string (inst, sl, (db_buf_t) "", 0, DV_DB_NULL);
	}
      END_DO_SET ();
    }
  else 
    {
      int inx;
      hs = (hash_source_t *)ts;
      DO_BOX (state_slot_t *, out, inx, hs->hs_out_slots)
	{
	  qst_set_bin_string (inst, out, (db_buf_t) "", 0, DV_DB_NULL);
	}
      END_DO_BOX;
    }

  qn_input (next_qn, inst, inst);
  /* the join test for this is in the 2nd end node after */
}


void
rdf_inf_pre_input (rdf_inf_pre_node_t * ri, caddr_t * inst,
		   caddr_t * volatile state)
{
  dk_set_t list;
  for (;;)
    {
      if (state)
	{
	  if (ri->ri_outer_any_passed)
	    qst_set (inst, ri->ri_outer_any_passed, NULL);
	  if (ri->ri_given)
	    {
	      list = ri_list (ri, ri->ri_given);
	      if (!list)
		{
		  if (RI_SUBCLASS == ri->ri_mode)
		    qst_set (inst, ri->ri_output, box_copy_tree (ri->ri_given));
		  else if (RI_SUBPROPERTY == ri->ri_mode)
		    qst_set (inst, ri->ri_output, box_copy_tree (ri->ri_given));
		  qn_send_output ((data_source_t *) ri, inst);
		  ri_outer_output (ri, inst);
		  return;
		}
	      qst_set (inst, ri->ri_output, box_copy_tree ((caddr_t) list->data));
	      if (list->next)
		{
		  SRC_IN_STATE ((data_source_t*)ri, inst) = inst;
		  inst[ri->ri_list_slot] = (caddr_t) list->next;
		  qn_send_output ((data_source_t *)ri, inst);
		  state = NULL;
		  continue;
		    }
	      else 
		{
		  SRC_IN_STATE ((data_source_t *)ri, inst) = NULL;;
		  qn_send_output ((data_source_t *) ri, inst);
		  ri_outer_output (ri, inst);
		  return;
		}
	    }
	  else 
	    {
	      /* see if the passing o/p has supers and generate the list of them if so */
	      if (((RI_SUPERCLASS == ri->ri_mode  || RI_SUBCLASS == ri->ri_mode)
		   && (!ri->ri_p || box_equal (rdfs_type, qst_get (inst, ri->ri_p)))
		   && (list = ri_list (ri, qst_get (inst, ri->ri_o))))
		  || ((RI_SUPERPROPERTY == ri->ri_mode || RI_SUBPROPERTY == ri->ri_mode)
		      && (list = ri_list (ri, qst_get (inst, ri->ri_p)))))
		{
		  if (list->next)
		    {
		      inst[ri->ri_list_slot] = (caddr_t) list->next;
		      SRC_IN_STATE ((data_source_t*) ri, inst) = inst;
		      qst_set (inst, ri->ri_output, box_copy_tree ((caddr_t) list->data));
		      qn_send_output ((data_source_t *)ri, inst);
		      state = NULL;
		      continue;
		    }
		  else
		    {
		      /* nothing to add.  output is the original o or p */
		      SRC_IN_STATE ((data_source_t *)ri, inst) = NULL;
		      if (RI_SUBCLASS == ri->ri_mode)
			qst_set (inst, ri->ri_output, box_copy_tree (qst_get (inst, ri->ri_o)));
		      else if (RI_SUBPROPERTY == ri->ri_mode)
			qst_set (inst, ri->ri_output, box_copy_tree (qst_get (inst, ri->ri_p)));
		      qn_send_output ((data_source_t *)ri, inst);
		      ri_outer_output (ri, inst);
		      return;
		    }
		}
	      else
		{
		  if (RI_SUBCLASS == ri->ri_mode)
		    qst_set (inst, ri->ri_output, box_copy_tree (qst_get (inst, ri->ri_o)));
		  else if (RI_SUBPROPERTY == ri->ri_mode)
		    qst_set (inst, ri->ri_output, box_copy_tree (qst_get (inst, ri->ri_p)));
		  qn_send_output ((data_source_t *) ri, inst);
		  ri_outer_output (ri, inst);
		  return;
		}
	    }
	}
      list = (dk_set_t) inst[ri->ri_list_slot];
      if (!list)
	{
	  SRC_IN_STATE ((data_source_t *) ri, inst) = NULL;
	  ri_outer_output (ri, inst);
	  return;
	}

      qst_set (inst, ri->ri_output, box_copy_tree ((caddr_t)list->data));
      if (!list->next)
	{
	  SRC_IN_STATE ((data_source_t*)ri, inst) = NULL;
	  qn_send_output ((data_source_t *)ri, inst);
	  ri_outer_output (ri, inst);
	  return;
	}
      inst[ri->ri_list_slot] = (caddr_t) list->next;
      SRC_IN_STATE ((data_source_t*)ri, inst) = inst;
      qn_send_output ((data_source_t *)ri, inst);
    }
}


id_hash_t * rdf_name_to_ric;

rdf_inf_ctx_t *
rdf_name_to_ctx (caddr_t name)
{
  rdf_inf_ctx_t ** place;
  if (!name)
    return NULL;
  place = (rdf_inf_ctx_t **) id_hash_get (rdf_name_to_ric, (caddr_t) &name);
  return place ? *place : NULL;
}


rdf_sub_t *
ric_iri_to_sub (rdf_inf_ctx_t * ctx, caddr_t iri)
{
  rdf_sub_t ** place = (rdf_sub_t **) id_hash_get (ctx->ric_iri_to_sub, (caddr_t)&iri);
  if (place)
    return *place;
  {
    NEW_VARZ (rdf_sub_t, rs);
    rs->rs_iri = box_copy (iri);
    id_hash_set (ctx->ric_iri_to_sub, (caddr_t)&rs->rs_iri, (caddr_t)&rs);
    return rs;
  }
}


void
dk_set_pushnew_equal (dk_set_t * s, void* box)
{
  DO_SET (caddr_t, elt, s)
    {
      if (box_equal (elt, (caddr_t) box))
	return;
    }
  END_DO_SET ();
  dk_set_push (s, box);
}


caddr_t
bif_rdf_inf_super (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t ctx_name = bif_string_arg (qst, args, 0, "rdf_inf_super");
  caddr_t super = bif_arg (qst, args, 1, "rdf_inf_super");
  caddr_t sub = bif_arg (qst, args, 2, "rdf_inf_super");
  int is_cl = bif_long_arg (qst, args, 3, "rdf_inf_super");
  rdf_inf_ctx_t ** place = (rdf_inf_ctx_t **) id_hash_get (rdf_name_to_ric, (caddr_t)&ctx_name);
  rdf_inf_ctx_t * ctx;
  rdf_sub_t * super_rs, * sub_rs;
  if (!place)
    {
      caddr_t n2 = box_copy (ctx_name);
      NEW_VARZ (rdf_inf_ctx_t, c1);
      c1->ric_name = n2;
      id_hash_set (rdf_name_to_ric, (caddr_t)&n2, (caddr_t)&c1);
      ctx = c1;
      ctx->ric_iri_to_sub = id_hash_allocate (61, sizeof (caddr_t), sizeof (caddr_t), treehash, treehashcmp);;
    }
  else
    ctx = * place;
  sub_rs = ric_iri_to_sub (ctx, sub);
  super_rs = ric_iri_to_sub (ctx, super);
  if (is_cl)
    {
      dk_set_pushnew_equal (&super_rs->rs_subclasses, box_copy (sub));
      dk_set_pushnew_equal (&sub_rs->rs_superclasses, box_copy (super));
    }
  else
    {
      dk_set_pushnew_equal (&super_rs->rs_subproperties, box_copy (sub));
      dk_set_pushnew_equal (&sub_rs->rs_superproperties, box_copy (super));
    }
  return 0;
}


caddr_t
bif_rdf_inf_clear (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t ctx_name = bif_string_arg (qst, args, 0, "rdf_inf_super");
  id_hash_remove (rdf_name_to_ric, (caddr_t)&ctx_name);
  return 0;
}


caddr_t
bif_rdf_inf_const_init (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t tn = box_dv_short_string ("http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
  rdfs_type = key_name_to_iri_id (bootstrap_cli->cli_trx, tn, 1);
  dk_free_box (tn);
  return 0;
}


void
rdf_inf_init ()
{
  rdf_name_to_ric = id_hash_allocate (61, sizeof (caddr_t), sizeof (caddr_t), treehash, treehashcmp);
  bif_define ("rdf_inf_super", bif_rdf_inf_super);
  bif_define ("rdf_inf_const_init", bif_rdf_inf_const_init);
  bif_define ("rdf_inf_clear", bif_rdf_inf_clear);
}


void
dk_set_ins_after (dk_set_t * s, void* point, void* new_elt)
{
  dk_set_t elt = *s;
  while (elt)
    {
      if (elt->data == point)
	{
	  elt->next = dk_set_cons (new_elt, elt->next);
	  return;
	}
      elt = elt->next;
    }
}


void
qn_ins_before (sql_comp_t * sc, data_source_t ** head, data_source_t * ins_before, data_source_t * new_qn)
{
  data_source_t * qn = *head;
  data_source_t * prev = NULL;
  dk_set_delete (&sc->sc_cc->cc_query->qr_nodes, (void*) new_qn);
  dk_set_ins_after (&sc->sc_cc->cc_query->qr_nodes, (void*) ins_before, (void*) new_qn);
  while (qn)
    {
      if (qn == ins_before)
	{
	  if (!prev)
	    *head = new_qn;
	  else
	    prev->src_continuations->data = (void *) new_qn;
	  new_qn->src_continuations = dk_set_cons ((void*) ins_before, NULL);
	  new_qn->src_pre_code = ins_before->src_pre_code;
	  ins_before->src_pre_code = NULL;
	  return;
	}
      prev = qn;
      qn = qn_next (qn);
    }
}


void
rdf_inf_pre_free (rdf_inf_pre_node_t * ri)
{
  dk_free_box (ri->ri_given);
}


rdf_inf_pre_node_t *
sqlg_rdf_inf_node (sql_comp_t *sc)
{
  SQL_NODE_INIT (rdf_inf_pre_node_t, ri, rdf_inf_pre_input, rdf_inf_pre_free);
  
  return ri;
}


void
sp_list_replace_ssl (search_spec_t * sp, state_slot_t * old, state_slot_t * new, int col_id)
{
  for (sp = sp; sp; sp = sp->sp_next)
    if (sp->sp_min_ssl == old && (!col_id || sp->sp_cl.cl_col_id == col_id))
      sp->sp_min_ssl = new;
}


void
sqlg_rdf_ts_replace_ssl (table_source_t * ts, state_slot_t * old, state_slot_t * new, int col_id, int inxop_inx)
{
  if (IS_TS (ts))
    {
      key_source_t * ks = ts->ts_order_ks;
      if (ts->ts_inx_op)
	{
	  inx_op_t * iop = ts->ts_inx_op->iop_terms[inxop_inx];
	  sp_list_replace_ssl (iop->iop_ks_start_spec.ksp_spec_array, old, new, col_id);
	  sp_list_replace_ssl (iop->iop_ks_full_spec.ksp_spec_array, old, new, col_id);
	  sp_list_replace_ssl (iop->iop_ks_row_spec, old, new, col_id);
	}
      else 
	{
	  sp_list_replace_ssl  (ks->ks_spec.ksp_spec_array, old, new, col_id);
	  sp_list_replace_ssl  (ks->ks_row_spec, old, new, col_id);
	}
    }
  else
    {
      hash_source_t * hs = (hash_source_t *) ts;
      int inx;
      DO_BOX (state_slot_t *, ref, inx, hs->hs_ref_slots)
	{
	  if (ref == old)
	    hs->hs_ref_slots[inx] = new;
	}
      END_DO_BOX;
    }
}


state_slot_t *
sqlg_col_ssl (df_elt_t * tb_dfe, char * name)
{
  DO_SET (df_elt_t *, out, &tb_dfe->_.table.out_cols)
    {
      if (0 == stricmp (out->_.col.col->col_name, name))
	return out->dfe_ssl;
    }
  END_DO_SET();
  return NULL;
}


void
sqlg_ri_post_filter (table_source_t * ts, df_elt_t * tb_dfe, rdf_inf_pre_node_t * ri, int p_check)
{
  /* this makes a folllow up node to 1. gs fp go to check that fp is rdfs_type. 
   * the end node will also record the fact of a result existing for an oj.  The post join test for an oj will also go here. */
  code_vec_t ajt;
  sql_comp_t * sc = tb_dfe->dfe_sqlo->so_sc;
  dk_set_t code = NULL;
  SQL_NODE_INIT (end_node_t, en, end_node_input, NULL);
  sql_node_append ((data_source_t**)&ts, (data_source_t*) en);
  if (p_check)
    {
      state_slot_t * p_ssl = sqlg_col_ssl (tb_dfe, "P");
      jmp_label_t is_p = sqlc_new_label (sc);
      jmp_label_t is_not_p = sqlc_new_label (sc);
      jmp_label_t bad_o = sqlc_new_label (sc);
      cv_compare (&code, BOP_EQ, p_ssl, ssl_new_constant (sc->sc_cc, box_copy_tree (rdfs_type)), is_p, is_not_p, is_not_p);
      cv_label (&code, is_not_p);
      cv_compare (&code, BOP_EQ, ri->ri_output, ri->ri_o, is_p, bad_o, bad_o);
      cv_label (&code, bad_o);
      cv_bret (&code, 0);
      cv_label (&code, is_p);
    }
  if (ri->ri_outer_any_passed)
    {
      cv_artm (&code, box_identity, ri->ri_outer_any_passed,  ssl_new_constant (sc->sc_cc, box_num (1)), NULL);
    }
  cv_bret (&code, 1);
  en->src_gen.src_after_test = code_to_cv (sc, code);
  if (ri->ri_outer_any_passed
      && (ajt = IS_TS (ts) ? ts->ts_after_join_test : ((hash_source_t *)ts)->hs_after_join_test))
    {
      SQL_NODE_INIT (end_node_t, en2, end_node_input, NULL);
      sql_node_append ((data_source_t **)&en, (data_source_t *)en2);
      en2->src_gen.src_after_test = ajt;
      if (IS_TS (ts))
	ts->ts_after_join_test = NULL;
      else
	((hash_source_t *) ts)->hs_after_join_test = NULL;
    }
  if (IS_TS (ts))
    ts->ts_is_outer = 0;
  else 
    ((hash_source_t *)ts)->hs_is_outer = 0;
}


void
sqlg_leading_subclass_inf (sqlo_t * so, data_source_t ** q_head, data_source_t * ts, df_elt_t * p_dfe, caddr_t p_const, df_elt_t * o_dfe, caddr_t o_iri,
			   rdf_inf_ctx_t * ctx, df_elt_t * tb_dfe, int inxop_inx)
{
  rdf_inf_pre_node_t * ri;
  if (p_const && !box_equal (rdfs_type, p_const))
    return;
  if (!p_const && !p_dfe && !sqlg_col_ssl (tb_dfe, "P"))
    return; /* if p is neither specified nor extracted, then do nothing.  P must ve specified or extracted if a dfe is for inference */
  ri = sqlg_rdf_inf_node (so->so_sc);
  qn_ins_before (tb_dfe->dfe_sqlo->so_sc, q_head, (data_source_t *)ts, (data_source_t *)ri);
  if (tb_dfe->_.table.ot->ot_is_outer)
    ri->ri_outer_any_passed = ssl_new_variable (tb_dfe->dfe_sqlo->so_sc->sc_cc, "any_passed", DV_LONG_INT);
  ri->ri_mode = RI_SUBCLASS;
  ri->ri_output = ssl_new_variable (o_dfe->dfe_sqlo->so_sc->sc_cc, "inferred", DV_IRI_ID);

  if (o_iri)
    ri->ri_given = box_copy_tree (o_iri);
  ri->ri_o = o_dfe->dfe_ssl;
  if (!p_const)
    {
      ri->ri_p = p_dfe ? p_dfe->dfe_ssl : sqlg_col_ssl (tb_dfe, "P");
      sqlg_ri_post_filter ((table_source_t *)ts, tb_dfe, ri, p_dfe ? 0 : 1);
      /* if the p is not fixed but still generating alternate O's, add node for filtering out hits with p != rdf:type */
    }
  else if (ri->ri_outer_any_passed)
    sqlg_ri_post_filter ((table_source_t *)ts, tb_dfe, ri, 0);
  sqlg_rdf_ts_replace_ssl ((table_source_t*) ts, ri->ri_o, ri->ri_output, 0, inxop_inx);
  if (!p_dfe)
    ri->ri_p = NULL; /* if P is open, all o's get expanded but the non-rdfs:type p's are filtered out afterwards */
  ri->ri_list_slot = cc_new_instance_slot (tb_dfe->dfe_sqlo->so_sc->sc_cc);
  ri->ri_ctx = ctx;
}


void
sqlg_trailing_subclass_inf (sqlo_t * so, data_source_t ** q_head, data_source_t * ts, df_elt_t * p_dfe, caddr_t p_const, df_elt_t * o_dfe, caddr_t o_iri,
			    rdf_inf_ctx_t * ctx, df_elt_t * tb_dfe, int inxop_inx)
{
  state_slot_t * o_slot;
  rdf_inf_pre_node_t * ri;
  if (p_const && !box_equal (rdfs_type, p_const))
    return;
  o_slot = sqlg_col_ssl (tb_dfe, "O");
  if (!o_slot)
    return; /* o is unspecified and but is not accessed */ 
  ri = sqlg_rdf_inf_node (so->so_sc);
  sql_node_append (q_head, (data_source_t *)ri);
  ri->ri_mode = RI_SUPERCLASS;
  ri->ri_output = o_slot;
  if (p_dfe)
    ri->ri_p = p_dfe->dfe_ssl;
  else 
    ri->ri_p = sqlg_col_ssl (tb_dfe, "P");
  ri->ri_o = ri->ri_output;
  ri->ri_list_slot = cc_new_instance_slot (tb_dfe->dfe_sqlo->so_sc->sc_cc);
  ri->ri_ctx = ctx;
}


void
sqlg_leading_subproperty_inf (sqlo_t * so, data_source_t ** q_head, data_source_t * ts, df_elt_t * p_dfe, caddr_t p_const, df_elt_t * o_dfe, caddr_t o_iri,
			   rdf_inf_ctx_t * ctx, df_elt_t * tb_dfe, int inxop_inx)
{
  rdf_inf_pre_node_t * ri;
  if (!p_const && !p_dfe && !sqlg_col_ssl (tb_dfe, "P"))
    return; /* if p is neither specified nor extracted, then do nothing.  P must ve specified or extracted if a dfe is for inference */
  ri = sqlg_rdf_inf_node (so->so_sc);
  qn_ins_before (tb_dfe->dfe_sqlo->so_sc, q_head, (data_source_t *)ts, (data_source_t *)ri);
  if (tb_dfe->_.table.ot->ot_is_outer)
    ri->ri_outer_any_passed = ssl_new_variable (tb_dfe->dfe_sqlo->so_sc->sc_cc, "any_passed", DV_LONG_INT);
  ri->ri_mode = RI_SUBPROPERTY;
  ri->ri_output = ssl_new_variable (tb_dfe->dfe_sqlo->so_sc->sc_cc, "inferred", DV_IRI_ID);

  if (p_const)
    ri->ri_given = box_copy_tree (p_const);
  else
    {
      ri->ri_p = p_dfe ? p_dfe->dfe_ssl : sqlg_col_ssl (tb_dfe, "P");
    }
  if (ri->ri_outer_any_passed)
    sqlg_ri_post_filter ((table_source_t *)ts, tb_dfe, ri, 0);
  ri->ri_p = p_dfe->dfe_ssl;
  sqlg_rdf_ts_replace_ssl ((table_source_t*) ts, ri->ri_p, ri->ri_output, 0, inxop_inx);
  ri->ri_list_slot = cc_new_instance_slot (tb_dfe->dfe_sqlo->so_sc->sc_cc);
  ri->ri_ctx = ctx;
}


void
sqlg_trailing_subproperty_inf (sqlo_t * so, data_source_t ** q_head, data_source_t * ts, df_elt_t * p_dfe, caddr_t p_const, df_elt_t * o_dfe, caddr_t o_iri,
			    rdf_inf_ctx_t * ctx, df_elt_t * tb_dfe, int inxop_inx)
{
  state_slot_t * p_slot;
  rdf_inf_pre_node_t * ri;
  p_slot = sqlg_col_ssl (tb_dfe, "P");
  if (!p_slot)
    return; /* P is unspecified and but is not accessed */ 
  ri = sqlg_rdf_inf_node (so->so_sc);
  sql_node_append (q_head, (data_source_t *)ri);
  ri->ri_mode = RI_SUPERPROPERTY;
  ri->ri_output = p_slot;
  ri->ri_p = ri->ri_output;
  ri->ri_list_slot = cc_new_instance_slot (tb_dfe->dfe_sqlo->so_sc->sc_cc);
  ri->ri_ctx = ctx;
}



#define LEADING_SUBCLASS \
  sqlg_leading_subclass_inf (tb_dfe->dfe_sqlo, q_head, ts, p_dfe, const_p, o_dfe, const_o, ctx, tb_dfe, inxop_inx)

#define TRAILING_SUBCLASS \
  sqlg_trailing_subclass_inf (tb_dfe->dfe_sqlo, q_head, ts, p_dfe, const_p, o_dfe, const_o, ctx, tb_dfe, inxop_inx)

#define LEADING_SUBP \
  sqlg_leading_subproperty_inf (tb_dfe->dfe_sqlo, q_head, ts, p_dfe, const_p, o_dfe, const_o, ctx, tb_dfe, inxop_inx)

#define TRAILING_SUBP \
  sqlg_trailing_subproperty_inf (tb_dfe->dfe_sqlo, q_head, ts, p_dfe, const_p, o_dfe, const_o, ctx, tb_dfe, inxop_inx)


caddr_t
dfe_iri_const (df_elt_t * dfe)
{
  caddr_t name;
  if (!dfe)
    return NULL;
  name = sqlo_iri_constant_name (dfe->dfe_tree);
  if (name)
    return key_name_to_iri_id (NULL, name, 0);
  return NULL;
}


void
sqlg_rdf_inf_1 (df_elt_t * tb_dfe, data_source_t * ts, data_source_t ** q_head, int inxop_inx)
{
  /* if the dfe is from rdf_quad and inference si on, recoggnize which  combinmation of spo is fixed and add the inf nodes before or after.  Works for table source and hash source  */
  dk_set_t col_preds;
  caddr_t ctx_name = sqlo_opt_value (tb_dfe->_.table.ot->ot_opts, OPT_RDF_INFERENCE);
  rdf_inf_ctx_t * ctx, **place;
  caddr_t const_s = NULL, const_p = NULL, const_o = NULL;
  df_elt_t * g_dfe = NULL, * s_dfe = NULL, * p_dfe = NULL, * o_dfe = NULL;
  state_slot_t * given_g = NULL, * given_s = NULL, * given_p = NULL, * given_o = NULL;
  if (!IS_TS (((table_source_t*)ts))
      && (qn_input_fn)hash_source_input != ts->src_input)
    return;
  if (-1 != inxop_inx)
    {
      df_inx_op_t * dio = (df_inx_op_t *)
	dk_set_nth (tb_dfe->_.table.inx_op->dio_terms, inxop_inx);
      tb_dfe = dio->dio_table;
    }
  if (stricmp (tb_dfe->_.table.ot->ot_table->tb_name, "DB.DBA.RDF_QUAD"))
    return;
  place = ctx_name ? (rdf_inf_ctx_t**)id_hash_get (rdf_name_to_ric, (caddr_t)&ctx_name) : NULL;
  if (!place)
    return;
  ctx = *place;
  col_preds = tb_dfe->_.table.col_preds;
  DO_SET (df_elt_t *, cp, &col_preds)
    {
      dbe_column_t * col = cp->_.bin.left->_.col.col;
      switch (col->col_name[0])
	{
	case 'G': g_dfe = cp->_.bin.right; break;
	case 'S': s_dfe = cp->_.bin.right; break;
	case 'P': p_dfe = cp->_.bin.right; break;
	case 'O': o_dfe = cp->_.bin.right; break;
	}
    }
  END_DO_SET();
  const_s = dfe_iri_const (s_dfe);
  const_p = dfe_iri_const (p_dfe);
  const_o = dfe_iri_const (o_dfe);
  if (!s_dfe && !p_dfe && !o_dfe)
    {
      TRAILING_SUBCLASS;
      TRAILING_SUBP;
    }
  else if (!s_dfe && !p_dfe && o_dfe)
    {
      LEADING_SUBCLASS;
      TRAILING_SUBP;
    }
  else if (!s_dfe && p_dfe && !o_dfe)
    {
      LEADING_SUBP;
      TRAILING_SUBCLASS;
    }
  else if (!s_dfe && p_dfe && o_dfe)
    {
      LEADING_SUBP;
      LEADING_SUBCLASS;
    }
  else if (s_dfe && !p_dfe && !o_dfe)
    {
      TRAILING_SUBCLASS;
      TRAILING_SUBP;
    }
  else if (s_dfe && !p_dfe && o_dfe)
    {
      LEADING_SUBCLASS;
      TRAILING_SUBP;
    }
  else if (s_dfe && p_dfe && !o_dfe)
    {
      LEADING_SUBP;
      TRAILING_SUBCLASS;
    }
  else if (s_dfe && p_dfe && o_dfe)
    {
      LEADING_SUBP;
      LEADING_SUBCLASS;
    }
  else 
    GPF_T1 (" all possibilities of spo already covered");
}

void
sqlg_rdf_inf (df_elt_t * tb_dfe, data_source_t * qn, data_source_t ** q_head)
{
  if (IS_TS ((table_source_t *)qn) && ((table_source_t *)qn)->ts_inx_op)
    {
      table_source_t * ts = (table_source_t *)qn;
      int inx;
      DO_BOX (inx_op_t *, iop, inx, ts->ts_inx_op->iop_terms)
	{
	  sqlg_rdf_inf_1 (tb_dfe, qn, q_head, inx);
	}
      END_DO_BOX;
    }
  else 
    sqlg_rdf_inf_1 (tb_dfe, qn, q_head, -1);
}
