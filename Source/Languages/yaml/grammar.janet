# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "yaml"
 :extras [comment]
 :conflicts [[_r_prp _r_sgl_prp]
             [_br_prp _br_sgl_prp]
             [_flw_seq_tal _sgl_flw_seq_tal]
             [_flw_map_tal _sgl_flw_map_tal]
             [_flw_ann_par_tal _sgl_flw_ann_par_tal]
             [_r_flw_seq_itm _r_sgl_flw_col_itm]
             [_r_flw_map_itm _r_sgl_flw_col_itm]
             [_r_flw_njl_ann_par _r_sgl_flw_njl_ann_par]
             [_r_flw_exp_par _r_sgl_flw_exp_par]
             [_r_dqt_str _r_sgl_dqt_str]
             [_r_sqt_str _r_sgl_sqt_str]
             [_r_pln_flw_val _r_sgl_pln_flw_val]
             [_r_prp]
             [_br_prp]]
 :precedences []
 :externals [_eof
             _s_dir_yml_bgn
             _r_dir_yml_ver
             _s_dir_tag_bgn
             _r_dir_tag_hdl
             _r_dir_tag_pfx
             _s_dir_rsv_bgn
             _r_dir_rsv_prm
             _s_drs_end
             _s_doc_end
             _r_blk_seq_bgn
             _br_blk_seq_bgn
             _b_blk_seq_bgn
             _r_blk_key_bgn
             _br_blk_key_bgn
             _b_blk_key_bgn
             _r_blk_val_bgn
             _br_blk_val_bgn
             _b_blk_val_bgn
             _r_blk_imp_bgn
             _r_blk_lit_bgn
             _br_blk_lit_bgn
             _r_blk_fld_bgn
             _br_blk_fld_bgn
             _br_blk_str_ctn
             _r_flw_seq_bgn
             _br_flw_seq_bgn
             _b_flw_seq_bgn
             _r_flw_seq_end
             _br_flw_seq_end
             _b_flw_seq_end
             _r_flw_map_bgn
             _br_flw_map_bgn
             _b_flw_map_bgn
             _r_flw_map_end
             _br_flw_map_end
             _b_flw_map_end
             _r_flw_sep_bgn
             _br_flw_sep_bgn
             _r_flw_key_bgn
             _br_flw_key_bgn
             _r_flw_jsv_bgn
             _br_flw_jsv_bgn
             _r_flw_njv_bgn
             _br_flw_njv_bgn
             _r_dqt_str_bgn
             _br_dqt_str_bgn
             _b_dqt_str_bgn
             _r_dqt_str_ctn
             _br_dqt_str_ctn
             _r_dqt_esc_nwl
             _br_dqt_esc_nwl
             _r_dqt_esc_seq
             _br_dqt_esc_seq
             _r_dqt_str_end
             _br_dqt_str_end
             _r_sqt_str_bgn
             _br_sqt_str_bgn
             _b_sqt_str_bgn
             _r_sqt_str_ctn
             _br_sqt_str_ctn
             _r_sqt_esc_sqt
             _br_sqt_esc_sqt
             _r_sqt_str_end
             _br_sqt_str_end
             _r_sgl_pln_nul_blk
             _br_sgl_pln_nul_blk
             _b_sgl_pln_nul_blk
             _r_sgl_pln_nul_flw
             _br_sgl_pln_nul_flw
             _r_sgl_pln_bol_blk
             _br_sgl_pln_bol_blk
             _b_sgl_pln_bol_blk
             _r_sgl_pln_bol_flw
             _br_sgl_pln_bol_flw
             _r_sgl_pln_int_blk
             _br_sgl_pln_int_blk
             _b_sgl_pln_int_blk
             _r_sgl_pln_int_flw
             _br_sgl_pln_int_flw
             _r_sgl_pln_flt_blk
             _br_sgl_pln_flt_blk
             _b_sgl_pln_flt_blk
             _r_sgl_pln_flt_flw
             _br_sgl_pln_flt_flw
             _r_sgl_pln_tms_blk
             _br_sgl_pln_tms_blk
             _b_sgl_pln_tms_blk
             _r_sgl_pln_tms_flw
             _br_sgl_pln_tms_flw
             _r_sgl_pln_str_blk
             _br_sgl_pln_str_blk
             _b_sgl_pln_str_blk
             _r_sgl_pln_str_flw
             _br_sgl_pln_str_flw
             _r_mtl_pln_str_blk
             _br_mtl_pln_str_blk
             _r_mtl_pln_str_flw
             _br_mtl_pln_str_flw
             _r_tag
             _br_tag
             _b_tag
             _r_acr_bgn
             _br_acr_bgn
             _b_acr_bgn
             _r_acr_ctn
             _r_als_bgn
             _br_als_bgn
             _b_als_bgn
             _r_als_ctn
             _bl
             comment
             _err_rec]
 :inline [_r_pln_blk
          _br_pln_blk
          _r_pln_flw
          _br_pln_flw
          _r_blk_seq_val
          _r_blk_map_val
          _r_flw_val_blk
          _br_flw_val_blk
          _r_sgl_flw_val_blk
          _br_sgl_flw_val_blk
          _b_sgl_flw_val_blk
          _r_flw_val_flw
          _br_flw_val_flw
          _r_sgl_flw_val_flw
          _r_flw_jsl_val
          _br_flw_jsl_val
          _r_sgl_flw_jsl_val
          _br_sgl_flw_jsl_val
          _b_sgl_flw_jsl_val
          _r_flw_njl_val_blk
          _br_flw_njl_val_blk
          _r_sgl_flw_njl_val_blk
          _br_sgl_flw_njl_val_blk
          _b_sgl_flw_njl_val_blk
          _r_flw_njl_val_flw
          _br_flw_njl_val_flw
          _r_sgl_flw_njl_val_flw]
 :supertypes []
 :rules
 {stream (:seq
          (:choice
           (:choice
            (:seq
             (:choice
              (:alias _bgn_imp_doc document)
              (:alias _drs_doc document)
              (:alias _exp_doc document))
             (:choice (:choice _doc_w_bgn_w_end_seq _doc_w_bgn_wo_end_seq) :blank))
            (:seq
             (:choice
              (:alias _bgn_imp_doc_end document)
              (:alias _drs_doc_end document)
              (:alias _exp_doc_end document)
              (:alias _doc_end document))
             (:choice
              (:choice
               _doc_w_bgn_w_end_seq
               _doc_w_bgn_wo_end_seq
               _doc_wo_bgn_w_end_seq
               _doc_wo_bgn_wo_end_seq)
              :blank)))
           :blank)
          _eof)
  _doc_w_bgn_w_end_seq (:seq
                        _doc_w_bgn_w_end
                        (:choice
                         (:choice
                          _doc_w_bgn_w_end_seq
                          _doc_w_bgn_wo_end_seq
                          _doc_wo_bgn_w_end_seq
                          _doc_wo_bgn_wo_end_seq)
                         :blank))
  _doc_w_bgn_wo_end_seq (:seq
                         _doc_w_bgn_wo_end
                         (:choice (:choice _doc_w_bgn_w_end_seq _doc_w_bgn_wo_end_seq) :blank))
  _doc_wo_bgn_w_end_seq (:seq
                         _doc_wo_bgn_w_end
                         (:choice
                          (:choice
                           _doc_w_bgn_w_end_seq
                           _doc_w_bgn_wo_end_seq
                           _doc_wo_bgn_w_end_seq
                           _doc_wo_bgn_wo_end_seq)
                          :blank))
  _doc_wo_bgn_wo_end_seq (:seq
                          _doc_wo_bgn_wo_end
                          (:choice (:choice _doc_w_bgn_w_end_seq _doc_w_bgn_wo_end_seq) :blank))
  _doc_w_bgn_w_end (:choice (:alias _exp_doc_end document) (:alias _doc_end document))
  _doc_w_bgn_wo_end (:alias _exp_doc document)
  _doc_wo_bgn_w_end (:choice (:alias _drs_doc_end document) (:alias _imp_doc_end document))
  _doc_wo_bgn_wo_end (:choice (:alias _drs_doc document) (:alias _imp_doc document))
  _bgn_imp_doc (:choice
                _exp_doc_tal
                (:alias _r_blk_seq_r_val block_node)
                (:alias _r_blk_map_r_val block_node))
  _drs_doc (:seq (:repeat1 _s_dir) _exp_doc)
  _exp_doc (:seq (:alias _s_drs_end "---") (:choice _exp_doc_tal :blank))
  _imp_doc (:choice
            (:alias _br_blk_seq_val block_node)
            (:alias _br_blk_map_val block_node)
            (:alias _br_blk_str_val block_node)
            _br_flw_val_blk)
  _drs_doc_end (:prec 1 (:seq _drs_doc (:alias _s_doc_end "...")))
  _exp_doc_end (:prec 1 (:seq _exp_doc (:alias _s_doc_end "...")))
  _imp_doc_end (:prec 1 (:seq _imp_doc (:alias _s_doc_end "...")))
  _bgn_imp_doc_end (:prec 1 (:seq _bgn_imp_doc (:alias _s_doc_end "...")))
  _doc_end (:alias _s_doc_end "...")
  _exp_doc_tal (:choice
                (:alias _r_blk_seq_br_val block_node)
                (:alias _br_blk_seq_val block_node)
                (:alias _r_blk_map_br_val block_node)
                (:alias _br_blk_map_val block_node)
                (:alias _r_blk_str_val block_node)
                (:alias _br_blk_str_val block_node)
                _r_flw_val_blk
                _br_flw_val_blk)
  _s_dir (:choice
          (:alias _s_dir_yml yaml_directive)
          (:alias _s_dir_tag tag_directive)
          (:alias _s_dir_rsv reserved_directive))
  _s_dir_yml (:seq _s_dir_yml_bgn (:alias _r_dir_yml_ver yaml_version))
  _s_dir_tag (:seq
              _s_dir_tag_bgn
              (:alias _r_dir_tag_hdl tag_handle)
              (:alias _r_dir_tag_pfx tag_prefix))
  _s_dir_rsv (:seq
              (:alias _s_dir_rsv_bgn directive_name)
              (:repeat (:alias _r_dir_rsv_prm directive_parameter)))
  _r_prp_val _r_prp
  _br_prp_val _br_prp
  _r_sgl_prp_val _r_sgl_prp
  _br_sgl_prp_val _br_sgl_prp
  _b_sgl_prp_val _b_sgl_prp
  _r_prp (:choice
          (:seq
           (:alias _r_acr anchor)
           (:choice (:choice (:alias _r_tag tag) (:alias _br_tag tag)) :blank))
          (:seq
           (:alias _r_tag tag)
           (:choice (:choice (:alias _r_acr anchor) (:alias _br_acr anchor)) :blank)))
  _br_prp (:choice
           (:seq
            (:alias _br_acr anchor)
            (:choice (:choice (:alias _r_tag tag) (:alias _br_tag tag)) :blank))
           (:seq
            (:alias _br_tag tag)
            (:choice (:choice (:alias _r_acr anchor) (:alias _br_acr anchor)) :blank)))
  _r_sgl_prp (:choice
              (:seq (:alias _r_acr anchor) (:choice (:alias _r_tag tag) :blank))
              (:seq (:alias _r_tag tag) (:choice (:alias _r_acr anchor) :blank)))
  _br_sgl_prp (:choice
               (:seq (:alias _br_acr anchor) (:choice (:alias _r_tag tag) :blank))
               (:seq (:alias _br_tag tag) (:choice (:alias _r_acr anchor) :blank)))
  _b_sgl_prp (:choice
              (:seq (:alias _b_acr anchor) (:choice (:alias _r_tag tag) :blank))
              (:seq (:alias _b_tag tag) (:choice (:alias _r_acr anchor) :blank)))
  _r_blk_seq_val (:choice
                  (:alias _r_blk_seq_r_val block_node)
                  (:alias _r_blk_seq_br_val block_node))
  _r_blk_seq_r_val (:alias _r_blk_seq block_sequence)
  _r_blk_seq_br_val (:seq _r_prp (:alias _br_blk_seq block_sequence))
  _br_blk_seq_val (:choice
                   (:alias _br_blk_seq block_sequence)
                   (:seq _br_prp (:alias _br_blk_seq block_sequence)))
  _r_blk_seq_spc_val (:seq _r_prp (:alias _b_blk_seq_spc block_sequence))
  _br_blk_seq_spc_val (:seq _br_prp (:alias _b_blk_seq_spc block_sequence))
  _b_blk_seq_spc_val (:alias _b_blk_seq_spc block_sequence)
  _r_blk_seq (:seq
              (:alias _r_blk_seq_itm block_sequence_item)
              (:repeat (:alias _b_blk_seq_itm block_sequence_item))
              _bl)
  _br_blk_seq (:seq
               (:alias _br_blk_seq_itm block_sequence_item)
               (:repeat (:alias _b_blk_seq_itm block_sequence_item))
               _bl)
  _b_blk_seq_spc (:seq (:repeat1 (:alias _b_blk_seq_itm block_sequence_item)) _bl)
  _r_blk_seq_itm (:seq (:alias _r_blk_seq_bgn "-") (:choice _blk_seq_itm_tal :blank))
  _br_blk_seq_itm (:seq (:alias _br_blk_seq_bgn "-") (:choice _blk_seq_itm_tal :blank))
  _b_blk_seq_itm (:seq (:alias _b_blk_seq_bgn "-") (:choice _blk_seq_itm_tal :blank))
  _blk_seq_itm_tal (:choice
                    _r_blk_seq_val
                    (:alias _br_blk_seq_val block_node)
                    _r_blk_map_val
                    (:alias _br_blk_map_val block_node)
                    (:alias _r_blk_str_val block_node)
                    (:alias _br_blk_str_val block_node)
                    _r_flw_val_blk
                    _br_flw_val_blk)
  _r_blk_map_val (:choice
                  (:alias _r_blk_map_r_val block_node)
                  (:alias _r_blk_map_br_val block_node))
  _r_blk_map_r_val (:alias _r_blk_map block_mapping)
  _r_blk_map_br_val (:seq _r_prp (:alias _br_blk_map block_mapping))
  _br_blk_map_val (:choice
                   (:alias _br_blk_map block_mapping)
                   (:seq _br_prp (:alias _br_blk_map block_mapping)))
  _r_blk_map (:seq _r_blk_map_itm (:repeat _b_blk_map_itm) _bl)
  _br_blk_map (:seq _br_blk_map_itm (:repeat _b_blk_map_itm) _bl)
  _r_blk_map_itm (:choice
                  (:alias _r_blk_exp_itm block_mapping_pair)
                  (:alias _r_blk_imp_itm block_mapping_pair))
  _br_blk_map_itm (:choice
                   (:alias _br_blk_exp_itm block_mapping_pair)
                   (:alias _br_blk_imp_itm block_mapping_pair))
  _b_blk_map_itm (:choice
                  (:alias _b_blk_exp_itm block_mapping_pair)
                  (:alias _b_blk_imp_itm block_mapping_pair))
  _r_blk_exp_itm (:prec-right 0
                  (:choice (:seq _r_blk_key_itm (:choice _b_blk_val_itm :blank)) _r_blk_val_itm))
  _br_blk_exp_itm (:prec-right 0
                   (:choice (:seq _br_blk_key_itm (:choice _b_blk_val_itm :blank)) _br_blk_val_itm))
  _b_blk_exp_itm (:prec-right 0
                  (:choice (:seq _b_blk_key_itm (:choice _b_blk_val_itm :blank)) _b_blk_val_itm))
  _r_blk_key_itm (:seq (:alias _r_blk_key_bgn "?") (:choice (:field :key _blk_exp_itm_tal) :blank))
  _br_blk_key_itm (:seq
                   (:alias _br_blk_key_bgn "?")
                   (:choice (:field :key _blk_exp_itm_tal) :blank))
  _b_blk_key_itm (:seq (:alias _b_blk_key_bgn "?") (:choice (:field :key _blk_exp_itm_tal) :blank))
  _r_blk_val_itm (:seq
                  (:alias _r_blk_val_bgn ":")
                  (:choice (:field :value _blk_exp_itm_tal) :blank))
  _br_blk_val_itm (:seq
                   (:alias _br_blk_val_bgn ":")
                   (:choice (:field :value _blk_exp_itm_tal) :blank))
  _b_blk_val_itm (:seq
                  (:alias _b_blk_val_bgn ":")
                  (:choice (:field :value _blk_exp_itm_tal) :blank))
  _r_blk_imp_itm (:seq (:field :key _r_sgl_flw_val_blk) _blk_imp_itm_tal)
  _br_blk_imp_itm (:seq (:field :key _br_sgl_flw_val_blk) _blk_imp_itm_tal)
  _b_blk_imp_itm (:seq (:field :key _b_sgl_flw_val_blk) _blk_imp_itm_tal)
  _blk_exp_itm_tal (:choice
                    _blk_seq_itm_tal
                    (:alias _r_blk_seq_spc_val block_node)
                    (:alias _br_blk_seq_spc_val block_node)
                    (:alias _b_blk_seq_spc_val block_node))
  _blk_imp_itm_tal (:seq
                    (:alias _r_blk_imp_bgn ":")
                    (:choice
                     (:field :value
                      (:choice
                       (:alias _r_blk_seq_br_val block_node)
                       (:alias _br_blk_seq_val block_node)
                       (:alias _r_blk_seq_spc_val block_node)
                       (:alias _br_blk_seq_spc_val block_node)
                       (:alias _b_blk_seq_spc_val block_node)
                       (:alias _r_blk_map_br_val block_node)
                       (:alias _br_blk_map_val block_node)
                       (:alias _r_blk_str_val block_node)
                       (:alias _br_blk_str_val block_node)
                       _r_flw_val_blk
                       _br_flw_val_blk))
                     :blank))
  _r_blk_str_val (:choice
                  (:alias _r_blk_str block_scalar)
                  (:seq
                   _r_prp
                   (:choice (:alias _r_blk_str block_scalar) (:alias _br_blk_str block_scalar))))
  _br_blk_str_val (:choice
                   (:alias _br_blk_str block_scalar)
                   (:seq
                    _br_prp
                    (:choice (:alias _r_blk_str block_scalar) (:alias _br_blk_str block_scalar))))
  _r_blk_str (:seq
              (:choice (:alias _r_blk_lit_bgn "|") (:alias _r_blk_fld_bgn ">"))
              (:repeat _br_blk_str_ctn)
              _bl)
  _br_blk_str (:seq
               (:choice (:alias _br_blk_lit_bgn "|") (:alias _br_blk_fld_bgn ">"))
               (:repeat _br_blk_str_ctn)
               _bl)
  _r_flw_val_blk (:choice _r_flw_jsl_val _r_flw_njl_val_blk)
  _br_flw_val_blk (:choice _br_flw_jsl_val _br_flw_njl_val_blk)
  _r_sgl_flw_val_blk (:choice _r_sgl_flw_jsl_val _r_sgl_flw_njl_val_blk)
  _br_sgl_flw_val_blk (:choice _br_sgl_flw_jsl_val _br_sgl_flw_njl_val_blk)
  _b_sgl_flw_val_blk (:choice _b_sgl_flw_jsl_val _b_sgl_flw_njl_val_blk)
  _r_flw_val_flw (:choice _r_flw_jsl_val _r_flw_njl_val_flw)
  _br_flw_val_flw (:choice _br_flw_jsl_val _br_flw_njl_val_flw)
  _r_sgl_flw_val_flw (:choice _r_sgl_flw_jsl_val _r_sgl_flw_njl_val_flw)
  _r_flw_jsl_val (:choice
                  (:alias _r_flw_seq_val flow_node)
                  (:alias _r_flw_map_val flow_node)
                  (:alias _r_dqt_str_val flow_node)
                  (:alias _r_sqt_str_val flow_node))
  _br_flw_jsl_val (:choice
                   (:alias _br_flw_seq_val flow_node)
                   (:alias _br_flw_map_val flow_node)
                   (:alias _br_dqt_str_val flow_node)
                   (:alias _br_sqt_str_val flow_node))
  _r_sgl_flw_jsl_val (:choice
                      (:alias _r_sgl_flw_seq_val flow_node)
                      (:alias _r_sgl_flw_map_val flow_node)
                      (:alias _r_sgl_dqt_str_val flow_node)
                      (:alias _r_sgl_sqt_str_val flow_node))
  _br_sgl_flw_jsl_val (:choice
                       (:alias _br_sgl_flw_seq_val flow_node)
                       (:alias _br_sgl_flw_map_val flow_node)
                       (:alias _br_sgl_dqt_str_val flow_node)
                       (:alias _br_sgl_sqt_str_val flow_node))
  _b_sgl_flw_jsl_val (:choice
                      (:alias _b_sgl_flw_seq_val flow_node)
                      (:alias _b_sgl_flw_map_val flow_node)
                      (:alias _b_sgl_dqt_str_val flow_node)
                      (:alias _b_sgl_sqt_str_val flow_node))
  _r_flw_njl_val_blk (:choice
                      (:alias _r_als_val flow_node)
                      (:alias _r_prp_val flow_node)
                      (:alias _r_pln_blk_val flow_node))
  _br_flw_njl_val_blk (:choice
                       (:alias _br_als_val flow_node)
                       (:alias _br_prp_val flow_node)
                       (:alias _br_pln_blk_val flow_node))
  _r_sgl_flw_njl_val_blk (:choice
                          (:alias _r_als_val flow_node)
                          (:alias _r_sgl_prp_val flow_node)
                          (:alias _r_sgl_pln_blk_val flow_node))
  _br_sgl_flw_njl_val_blk (:choice
                           (:alias _br_als_val flow_node)
                           (:alias _br_sgl_prp_val flow_node)
                           (:alias _br_sgl_pln_blk_val flow_node))
  _b_sgl_flw_njl_val_blk (:choice
                          (:alias _b_als_val flow_node)
                          (:alias _b_sgl_prp_val flow_node)
                          (:alias _b_sgl_pln_blk_val flow_node))
  _r_flw_njl_val_flw (:choice
                      (:alias _r_als_val flow_node)
                      (:alias _r_prp_val flow_node)
                      (:alias _r_pln_flw_val flow_node))
  _br_flw_njl_val_flw (:choice
                       (:alias _br_als_val flow_node)
                       (:alias _br_prp_val flow_node)
                       (:alias _br_pln_flw_val flow_node))
  _r_sgl_flw_njl_val_flw (:choice
                          (:alias _r_als_val flow_node)
                          (:alias _r_sgl_prp_val flow_node)
                          (:alias _r_sgl_pln_flw_val flow_node))
  _r_flw_seq_val (:choice
                  (:alias _r_flw_seq flow_sequence)
                  (:seq
                   _r_prp
                   (:choice (:alias _r_flw_seq flow_sequence) (:alias _br_flw_seq flow_sequence))))
  _br_flw_seq_val (:choice
                   (:alias _br_flw_seq flow_sequence)
                   (:seq
                    _br_prp
                    (:choice (:alias _r_flw_seq flow_sequence) (:alias _br_flw_seq flow_sequence))))
  _r_sgl_flw_seq_val (:choice
                      (:alias _r_sgl_flw_seq flow_sequence)
                      (:seq _r_sgl_prp (:alias _r_sgl_flw_seq flow_sequence)))
  _br_sgl_flw_seq_val (:choice
                       (:alias _br_sgl_flw_seq flow_sequence)
                       (:seq _br_sgl_prp (:alias _r_sgl_flw_seq flow_sequence)))
  _b_sgl_flw_seq_val (:choice
                      (:alias _b_sgl_flw_seq flow_sequence)
                      (:seq _b_sgl_prp (:alias _r_sgl_flw_seq flow_sequence)))
  _r_flw_seq (:seq (:alias _r_flw_seq_bgn "[") _flw_seq_tal)
  _br_flw_seq (:seq (:alias _br_flw_seq_bgn "[") _flw_seq_tal)
  _r_sgl_flw_seq (:seq (:alias _r_flw_seq_bgn "[") _sgl_flw_seq_tal)
  _br_sgl_flw_seq (:seq (:alias _br_flw_seq_bgn "[") _sgl_flw_seq_tal)
  _b_sgl_flw_seq (:seq (:alias _b_flw_seq_bgn "[") _sgl_flw_seq_tal)
  _flw_seq_tal (:seq
                (:choice (:choice _r_flw_seq_dat _br_flw_seq_dat) :blank)
                (:choice
                 (:alias _r_flw_seq_end "]")
                 (:alias _br_flw_seq_end "]")
                 (:alias _b_flw_seq_end "]")))
  _sgl_flw_seq_tal (:seq (:choice _r_sgl_flw_col_dat :blank) (:alias _r_flw_seq_end "]"))
  _r_flw_map_val (:choice
                  (:alias _r_flw_map flow_mapping)
                  (:seq
                   _r_prp
                   (:choice (:alias _r_flw_map flow_mapping) (:alias _br_flw_map flow_mapping))))
  _br_flw_map_val (:choice
                   (:alias _br_flw_map flow_mapping)
                   (:seq
                    _br_prp
                    (:choice (:alias _r_flw_map flow_mapping) (:alias _br_flw_map flow_mapping))))
  _r_sgl_flw_map_val (:choice
                      (:alias _r_sgl_flw_map flow_mapping)
                      (:seq _r_sgl_prp (:alias _r_sgl_flw_map flow_mapping)))
  _br_sgl_flw_map_val (:choice
                       (:alias _br_sgl_flw_map flow_mapping)
                       (:seq _br_sgl_prp (:alias _r_sgl_flw_map flow_mapping)))
  _b_sgl_flw_map_val (:choice
                      (:alias _b_sgl_flw_map flow_mapping)
                      (:seq _b_sgl_prp (:alias _r_sgl_flw_map flow_mapping)))
  _r_flw_map (:seq (:alias _r_flw_map_bgn "{") _flw_map_tal)
  _br_flw_map (:seq (:alias _br_flw_map_bgn "{") _flw_map_tal)
  _r_sgl_flw_map (:seq (:alias _r_flw_map_bgn "{") _sgl_flw_map_tal)
  _br_sgl_flw_map (:seq (:alias _br_flw_map_bgn "{") _sgl_flw_map_tal)
  _b_sgl_flw_map (:seq (:alias _b_flw_map_bgn "{") _sgl_flw_map_tal)
  _flw_map_tal (:seq
                (:choice (:choice _r_flw_map_dat _br_flw_map_dat) :blank)
                (:choice
                 (:alias _r_flw_map_end "}")
                 (:alias _br_flw_map_end "}")
                 (:alias _b_flw_map_end "}")))
  _sgl_flw_map_tal (:seq (:choice _r_sgl_flw_col_dat :blank) (:alias _r_flw_map_end "}"))
  _r_flw_seq_dat (:seq
                  _r_flw_seq_itm
                  (:repeat _flw_seq_dat_rpt)
                  (:choice
                   (:choice (:alias _r_flw_sep_bgn ",") (:alias _br_flw_sep_bgn ","))
                   :blank))
  _br_flw_seq_dat (:seq
                   _br_flw_seq_itm
                   (:repeat _flw_seq_dat_rpt)
                   (:choice
                    (:choice (:alias _r_flw_sep_bgn ",") (:alias _br_flw_sep_bgn ","))
                    :blank))
  _r_flw_map_dat (:seq
                  _r_flw_map_itm
                  (:repeat _flw_map_dat_rpt)
                  (:choice
                   (:choice (:alias _r_flw_sep_bgn ",") (:alias _br_flw_sep_bgn ","))
                   :blank))
  _br_flw_map_dat (:seq
                   _br_flw_map_itm
                   (:repeat _flw_map_dat_rpt)
                   (:choice
                    (:choice (:alias _r_flw_sep_bgn ",") (:alias _br_flw_sep_bgn ","))
                    :blank))
  _r_sgl_flw_col_dat (:seq
                      _r_sgl_flw_col_itm
                      (:repeat _sgl_flw_col_dat_rpt)
                      (:choice (:alias _r_flw_sep_bgn ",") :blank))
  _flw_seq_dat_rpt (:seq
                    (:choice (:alias _r_flw_sep_bgn ",") (:alias _br_flw_sep_bgn ","))
                    (:choice _r_flw_seq_itm _br_flw_seq_itm))
  _flw_map_dat_rpt (:seq
                    (:choice (:alias _r_flw_sep_bgn ",") (:alias _br_flw_sep_bgn ","))
                    (:choice _r_flw_map_itm _br_flw_map_itm))
  _sgl_flw_col_dat_rpt (:seq (:alias _r_flw_sep_bgn ",") _r_sgl_flw_col_itm)
  _r_flw_seq_itm (:choice
                  _r_flw_val_flw
                  (:alias _r_flw_exp_par flow_pair)
                  (:alias _r_flw_imp_r_par flow_pair)
                  (:alias _r_flw_njl_ann_par flow_pair))
  _br_flw_seq_itm (:choice
                   _br_flw_val_flw
                   (:alias _br_flw_exp_par flow_pair)
                   (:alias _br_flw_imp_r_par flow_pair)
                   (:alias _br_flw_njl_ann_par flow_pair))
  _r_flw_map_itm (:choice
                  _r_flw_val_flw
                  (:alias _r_flw_exp_par flow_pair)
                  (:alias _r_flw_imp_r_par flow_pair)
                  (:alias _r_flw_imp_br_par flow_pair)
                  (:alias _r_flw_njl_ann_par flow_pair))
  _br_flw_map_itm (:choice
                   _br_flw_val_flw
                   (:alias _br_flw_exp_par flow_pair)
                   (:alias _br_flw_imp_r_par flow_pair)
                   (:alias _br_flw_imp_br_par flow_pair)
                   (:alias _br_flw_njl_ann_par flow_pair))
  _r_sgl_flw_col_itm (:choice
                      _r_sgl_flw_val_flw
                      (:alias _r_sgl_flw_exp_par flow_pair)
                      (:alias _r_sgl_flw_imp_par flow_pair)
                      (:alias _r_sgl_flw_njl_ann_par flow_pair))
  _r_flw_exp_par (:seq
                  (:alias _r_flw_key_bgn "?")
                  (:choice
                   (:choice _r_flw_imp_r_par _r_flw_imp_br_par _br_flw_imp_r_par _br_flw_imp_br_par)
                   :blank))
  _br_flw_exp_par (:seq
                   (:alias _br_flw_key_bgn "?")
                   (:choice
                    (:choice
                     _r_flw_imp_r_par
                     _r_flw_imp_br_par
                     _br_flw_imp_r_par
                     _br_flw_imp_br_par)
                    :blank))
  _r_sgl_flw_exp_par (:seq (:alias _r_flw_key_bgn "?") (:choice _r_sgl_flw_imp_par :blank))
  _r_flw_imp_r_par (:choice
                    (:seq (:field :key _r_flw_jsl_val) _r_flw_jsl_ann_par)
                    (:seq (:field :key _r_flw_njl_val_flw) _r_flw_njl_ann_par))
  _r_flw_imp_br_par (:choice
                     (:seq (:field :key _r_flw_jsl_val) _br_flw_jsl_ann_par)
                     (:seq (:field :key _r_flw_njl_val_flw) _br_flw_njl_ann_par))
  _br_flw_imp_r_par (:choice
                     (:seq (:field :key _br_flw_jsl_val) _r_flw_jsl_ann_par)
                     (:seq (:field :key _br_flw_njl_val_flw) _r_flw_njl_ann_par))
  _br_flw_imp_br_par (:choice
                      (:seq (:field :key _br_flw_jsl_val) _br_flw_jsl_ann_par)
                      (:seq (:field :key _br_flw_njl_val_flw) _br_flw_njl_ann_par))
  _r_sgl_flw_imp_par (:choice
                      (:seq (:field :key _r_sgl_flw_jsl_val) _r_sgl_flw_jsl_ann_par)
                      (:seq (:field :key _r_sgl_flw_njl_val_flw) _r_sgl_flw_njl_ann_par))
  _r_flw_jsl_ann_par (:seq
                      (:alias _r_flw_jsv_bgn ":")
                      (:choice (:field :value _flw_ann_par_tal) :blank))
  _br_flw_jsl_ann_par (:seq
                       (:alias _br_flw_jsv_bgn ":")
                       (:choice (:field :value _flw_ann_par_tal) :blank))
  _r_sgl_flw_jsl_ann_par (:seq
                          (:alias _r_flw_jsv_bgn ":")
                          (:choice (:field :value _sgl_flw_ann_par_tal) :blank))
  _r_flw_njl_ann_par (:seq
                      (:alias _r_flw_njv_bgn ":")
                      (:choice (:field :value _flw_ann_par_tal) :blank))
  _br_flw_njl_ann_par (:seq
                       (:alias _br_flw_njv_bgn ":")
                       (:choice (:field :value _flw_ann_par_tal) :blank))
  _r_sgl_flw_njl_ann_par (:seq
                          (:alias _r_flw_njv_bgn ":")
                          (:choice (:field :value _sgl_flw_ann_par_tal) :blank))
  _flw_ann_par_tal (:choice _r_flw_val_flw _br_flw_val_flw)
  _sgl_flw_ann_par_tal _r_sgl_flw_val_flw
  _r_dqt_str_val (:choice
                  (:alias _r_dqt_str double_quote_scalar)
                  (:seq
                   _r_prp
                   (:choice
                    (:alias _r_dqt_str double_quote_scalar)
                    (:alias _br_dqt_str double_quote_scalar))))
  _br_dqt_str_val (:choice
                   (:alias _br_dqt_str double_quote_scalar)
                   (:seq
                    _br_prp
                    (:choice
                     (:alias _r_dqt_str double_quote_scalar)
                     (:alias _br_dqt_str double_quote_scalar))))
  _r_sgl_dqt_str_val (:choice
                      (:alias _r_sgl_dqt_str double_quote_scalar)
                      (:seq _r_sgl_prp (:alias _r_sgl_dqt_str double_quote_scalar)))
  _br_sgl_dqt_str_val (:choice
                       (:alias _br_sgl_dqt_str double_quote_scalar)
                       (:seq _br_sgl_prp (:alias _r_sgl_dqt_str double_quote_scalar)))
  _b_sgl_dqt_str_val (:choice
                      (:alias _b_sgl_dqt_str double_quote_scalar)
                      (:seq _b_sgl_prp (:alias _r_sgl_dqt_str double_quote_scalar)))
  _r_dqt_str (:seq
              (:alias _r_dqt_str_bgn "\"")
              (:choice _r_sgl_dqt_ctn :blank)
              (:choice (:alias _r_dqt_esc_nwl escape_sequence) :blank)
              (:repeat _br_mtl_dqt_ctn)
              (:choice (:alias _r_dqt_str_end "\"") (:alias _br_dqt_str_end "\"")))
  _br_dqt_str (:seq
               (:alias _br_dqt_str_bgn "\"")
               (:choice _r_sgl_dqt_ctn :blank)
               (:choice (:alias _r_dqt_esc_nwl escape_sequence) :blank)
               (:repeat _br_mtl_dqt_ctn)
               (:choice (:alias _r_dqt_str_end "\"") (:alias _br_dqt_str_end "\"")))
  _r_sgl_dqt_str (:seq
                  (:alias _r_dqt_str_bgn "\"")
                  (:choice _r_sgl_dqt_ctn :blank)
                  (:alias _r_dqt_str_end "\""))
  _br_sgl_dqt_str (:seq
                   (:alias _br_dqt_str_bgn "\"")
                   (:choice _r_sgl_dqt_ctn :blank)
                   (:alias _r_dqt_str_end "\""))
  _b_sgl_dqt_str (:seq
                  (:alias _b_dqt_str_bgn "\"")
                  (:choice _r_sgl_dqt_ctn :blank)
                  (:alias _r_dqt_str_end "\""))
  _r_sgl_dqt_ctn (:repeat1 (:choice _r_dqt_str_ctn (:alias _r_dqt_esc_seq escape_sequence)))
  _br_mtl_dqt_ctn (:choice
                   (:alias _br_dqt_esc_nwl escape_sequence)
                   (:seq
                    (:choice _br_dqt_str_ctn (:alias _br_dqt_esc_seq escape_sequence))
                    (:repeat (:choice _r_dqt_str_ctn (:alias _r_dqt_esc_seq escape_sequence)))
                    (:choice (:alias _r_dqt_esc_nwl escape_sequence) :blank)))
  _r_sqt_str_val (:choice
                  (:alias _r_sqt_str single_quote_scalar)
                  (:seq
                   _r_prp
                   (:choice
                    (:alias _r_sqt_str single_quote_scalar)
                    (:alias _br_sqt_str single_quote_scalar))))
  _br_sqt_str_val (:choice
                   (:alias _br_sqt_str single_quote_scalar)
                   (:seq
                    _br_prp
                    (:choice
                     (:alias _r_sqt_str single_quote_scalar)
                     (:alias _br_sqt_str single_quote_scalar))))
  _r_sgl_sqt_str_val (:choice
                      (:alias _r_sgl_sqt_str single_quote_scalar)
                      (:seq _r_sgl_prp (:alias _r_sgl_sqt_str single_quote_scalar)))
  _br_sgl_sqt_str_val (:choice
                       (:alias _br_sgl_sqt_str single_quote_scalar)
                       (:seq _br_sgl_prp (:alias _r_sgl_sqt_str single_quote_scalar)))
  _b_sgl_sqt_str_val (:choice
                      (:alias _b_sgl_sqt_str single_quote_scalar)
                      (:seq _b_sgl_prp (:alias _r_sgl_sqt_str single_quote_scalar)))
  _r_sqt_str (:seq
              (:alias _r_sqt_str_bgn "'")
              (:choice _r_sgl_sqt_ctn :blank)
              (:repeat _br_mtl_sqt_ctn)
              (:choice (:alias _r_sqt_str_end "'") (:alias _br_sqt_str_end "'")))
  _br_sqt_str (:seq
               (:alias _br_sqt_str_bgn "'")
               (:choice _r_sgl_sqt_ctn :blank)
               (:repeat _br_mtl_sqt_ctn)
               (:choice (:alias _r_sqt_str_end "'") (:alias _br_sqt_str_end "'")))
  _r_sgl_sqt_str (:seq
                  (:alias _r_sqt_str_bgn "'")
                  (:choice _r_sgl_sqt_ctn :blank)
                  (:alias _r_sqt_str_end "'"))
  _br_sgl_sqt_str (:seq
                   (:alias _br_sqt_str_bgn "'")
                   (:choice _r_sgl_sqt_ctn :blank)
                   (:alias _r_sqt_str_end "'"))
  _b_sgl_sqt_str (:seq
                  (:alias _b_sqt_str_bgn "'")
                  (:choice _r_sgl_sqt_ctn :blank)
                  (:alias _r_sqt_str_end "'"))
  _r_sgl_sqt_ctn (:repeat1 (:choice _r_sqt_str_ctn (:alias _r_sqt_esc_sqt escape_sequence)))
  _br_mtl_sqt_ctn (:seq
                   (:choice _br_sqt_str_ctn (:alias _br_sqt_esc_sqt escape_sequence))
                   (:repeat (:choice _r_sqt_str_ctn (:alias _r_sqt_esc_sqt escape_sequence))))
  _r_pln_blk_val (:choice _r_pln_blk (:seq _r_prp (:choice _r_pln_blk _br_pln_blk)))
  _br_pln_blk_val (:choice _br_pln_blk (:seq _br_prp (:choice _r_pln_blk _br_pln_blk)))
  _r_sgl_pln_blk_val (:choice
                      (:alias _r_sgl_pln_blk plain_scalar)
                      (:seq _r_sgl_prp (:alias _r_sgl_pln_blk plain_scalar)))
  _br_sgl_pln_blk_val (:choice
                       (:alias _br_sgl_pln_blk plain_scalar)
                       (:seq _br_sgl_prp (:alias _r_sgl_pln_blk plain_scalar)))
  _b_sgl_pln_blk_val (:choice
                      (:alias _b_sgl_pln_blk plain_scalar)
                      (:seq _b_sgl_prp (:alias _r_sgl_pln_blk plain_scalar)))
  _r_pln_blk (:choice (:alias _r_sgl_pln_blk plain_scalar) (:alias _r_mtl_pln_blk plain_scalar))
  _br_pln_blk (:choice (:alias _br_sgl_pln_blk plain_scalar) (:alias _br_mtl_pln_blk plain_scalar))
  _r_pln_flw_val (:choice _r_pln_flw (:seq _r_prp (:choice _r_pln_flw _br_pln_flw)))
  _br_pln_flw_val (:choice _br_pln_flw (:seq _br_prp (:choice _r_pln_flw _br_pln_flw)))
  _r_sgl_pln_flw_val (:choice
                      (:alias _r_sgl_pln_flw plain_scalar)
                      (:seq _r_sgl_prp (:alias _r_sgl_pln_flw plain_scalar)))
  _r_pln_flw (:choice (:alias _r_sgl_pln_flw plain_scalar) (:alias _r_mtl_pln_flw plain_scalar))
  _br_pln_flw (:choice (:alias _br_sgl_pln_flw plain_scalar) (:alias _br_mtl_pln_flw plain_scalar))
  _r_sgl_pln_blk (:choice
                  (:alias _r_sgl_pln_nul_blk null_scalar)
                  (:alias _r_sgl_pln_bol_blk boolean_scalar)
                  (:alias _r_sgl_pln_int_blk integer_scalar)
                  (:alias _r_sgl_pln_flt_blk float_scalar)
                  (:alias _r_sgl_pln_tms_blk timestamp_scalar)
                  (:alias _r_sgl_pln_str_blk string_scalar))
  _br_sgl_pln_blk (:choice
                   (:alias _br_sgl_pln_nul_blk null_scalar)
                   (:alias _br_sgl_pln_bol_blk boolean_scalar)
                   (:alias _br_sgl_pln_int_blk integer_scalar)
                   (:alias _br_sgl_pln_flt_blk float_scalar)
                   (:alias _br_sgl_pln_tms_blk timestamp_scalar)
                   (:alias _br_sgl_pln_str_blk string_scalar))
  _b_sgl_pln_blk (:choice
                  (:alias _b_sgl_pln_nul_blk null_scalar)
                  (:alias _b_sgl_pln_bol_blk boolean_scalar)
                  (:alias _b_sgl_pln_int_blk integer_scalar)
                  (:alias _b_sgl_pln_flt_blk float_scalar)
                  (:alias _b_sgl_pln_tms_blk timestamp_scalar)
                  (:alias _b_sgl_pln_str_blk string_scalar))
  _r_sgl_pln_flw (:choice
                  (:alias _r_sgl_pln_nul_flw null_scalar)
                  (:alias _r_sgl_pln_bol_flw boolean_scalar)
                  (:alias _r_sgl_pln_int_flw integer_scalar)
                  (:alias _r_sgl_pln_flt_flw float_scalar)
                  (:alias _r_sgl_pln_tms_flw timestamp_scalar)
                  (:alias _r_sgl_pln_str_flw string_scalar))
  _br_sgl_pln_flw (:choice
                   (:alias _br_sgl_pln_nul_flw null_scalar)
                   (:alias _br_sgl_pln_bol_flw boolean_scalar)
                   (:alias _br_sgl_pln_int_flw integer_scalar)
                   (:alias _br_sgl_pln_flt_flw float_scalar)
                   (:alias _br_sgl_pln_tms_flw timestamp_scalar)
                   (:alias _br_sgl_pln_str_flw string_scalar))
  _r_mtl_pln_blk (:alias _r_mtl_pln_str_blk string_scalar)
  _br_mtl_pln_blk (:alias _br_mtl_pln_str_blk string_scalar)
  _r_mtl_pln_flw (:alias _r_mtl_pln_str_flw string_scalar)
  _br_mtl_pln_flw (:alias _br_mtl_pln_str_flw string_scalar)
  _r_als_val (:alias _r_als alias)
  _br_als_val (:alias _br_als alias)
  _b_als_val (:alias _b_als alias)
  _r_als (:seq (:alias _r_als_bgn "*") (:alias _r_als_ctn alias_name))
  _br_als (:seq (:alias _br_als_bgn "*") (:alias _r_als_ctn alias_name))
  _b_als (:seq (:alias _b_als_bgn "*") (:alias _r_als_ctn alias_name))
  _r_acr (:seq (:alias _r_acr_bgn "&") (:alias _r_acr_ctn anchor_name))
  _br_acr (:seq (:alias _br_acr_bgn "&") (:alias _r_acr_ctn anchor_name))
  _b_acr (:seq (:alias _b_acr_bgn "&") (:alias _r_acr_ctn anchor_name))}}
