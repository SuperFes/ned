# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "sql"
 :word _identifier
 :extras [(:pattern "\\s\\n") (:pattern "\\s") comment marginalia]
 :conflicts [[object_reference _qualified_field]
             [object_reference]
             [between_expression binary_expression]
             [time]
             [timestamp]]
 :precedences [["binary_is"
                "unary_not"
                "binary_exp"
                "binary_times"
                "binary_plus"
                "unary_other"
                "binary_other"
                "binary_in"
                "binary_compare"
                "binary_relation"
                "pattern_matching"
                "between"
                "clause_connective"
                "clause_disjunctive"]]
 :externals [_dollar_quoted_string_start_tag _dollar_quoted_string_end_tag _dollar_quoted_string]
 :inline []
 :supertypes []
 :rules
 {program (:seq
           (:repeat (:seq (:choice transaction statement block) ";"))
           (:choice statement :blank))
  keyword_select (:pattern "[sS][eE][lL][eE][cC][tT]")
  keyword_delete (:pattern "[dD][eE][lL][eE][tT][eE]")
  keyword_insert (:pattern "[iI][nN][sS][eE][rR][tT]")
  keyword_replace (:pattern "[rR][eE][pP][lL][aA][cC][eE]")
  keyword_update (:pattern "[uU][pP][dD][aA][tT][eE]")
  keyword_truncate (:pattern "[tT][rR][uU][nN][cC][aA][tT][eE]")
  keyword_merge (:pattern "[mM][eE][rR][gG][eE]")
  keyword_show (:pattern "[sS][hH][oO][wW]")
  keyword_unload (:pattern "[uU][nN][lL][oO][aA][dD]")
  keyword_into (:pattern "[iI][nN][tT][oO]")
  keyword_overwrite (:pattern "[oO][vV][eE][rR][wW][rR][iI][tT][eE]")
  keyword_values (:pattern "[vV][aA][lL][uU][eE][sS]")
  keyword_value (:pattern "[vV][aA][lL][uU][eE]")
  keyword_matched (:pattern "[mM][aA][tT][cC][hH][eE][dD]")
  keyword_set (:pattern "[sS][eE][tT]")
  keyword_from (:pattern "[fF][rR][oO][mM]")
  keyword_left (:pattern "[lL][eE][fF][tT]")
  keyword_right (:pattern "[rR][iI][gG][hH][tT]")
  keyword_inner (:pattern "[iI][nN][nN][eE][rR]")
  keyword_full (:pattern "[fF][uU][lL][lL]")
  keyword_outer (:pattern "[oO][uU][tT][eE][rR]")
  keyword_cross (:pattern "[cC][rR][oO][sS][sS]")
  keyword_join (:pattern "[jJ][oO][iI][nN]")
  keyword_lateral (:pattern "[lL][aA][tT][eE][rR][aA][lL]")
  keyword_natural (:pattern "[nN][aA][tT][uU][rR][aA][lL]")
  keyword_on (:pattern "[oO][nN]")
  keyword_off (:pattern "[oO][fF][fF]")
  keyword_where (:pattern "[wW][hH][eE][rR][eE]")
  keyword_order (:pattern "[oO][rR][dD][eE][rR]")
  keyword_group (:pattern "[gG][rR][oO][uU][pP]")
  keyword_partition (:pattern "[pP][aA][rR][tT][iI][tT][iI][oO][nN]")
  keyword_by (:pattern "[bB][yY]")
  keyword_having (:pattern "[hH][aA][vV][iI][nN][gG]")
  keyword_desc (:pattern "[dD][eE][sS][cC]")
  keyword_asc (:pattern "[aA][sS][cC]")
  keyword_limit (:pattern "[lL][iI][mM][iI][tT]")
  keyword_offset (:pattern "[oO][fF][fF][sS][eE][tT]")
  keyword_primary (:pattern "[pP][rR][iI][mM][aA][rR][yY]")
  keyword_create (:pattern "[cC][rR][eE][aA][tT][eE]")
  keyword_alter (:pattern "[aA][lL][tT][eE][rR]")
  keyword_change (:pattern "[cC][hH][aA][nN][gG][eE]")
  keyword_analyze (:pattern "[aA][nN][aA][lL][yY][zZ][eE]")
  keyword_explain (:pattern "[eE][xX][pP][lL][aA][iI][nN]")
  keyword_verbose (:pattern "[vV][eE][rR][bB][oO][sS][eE]")
  keyword_modify (:pattern "[mM][oO][dD][iI][fF][yY]")
  keyword_drop (:pattern "[dD][rR][oO][pP]")
  keyword_add (:pattern "[aA][dD][dD]")
  keyword_table (:pattern "[tT][aA][bB][lL][eE]")
  keyword_tables (:pattern "[tT][aA][bB][lL][eE][sS]")
  keyword_view (:pattern "[vV][iI][eE][wW]")
  keyword_column (:pattern "[cC][oO][lL][uU][mM][nN]")
  keyword_columns (:pattern "[cC][oO][lL][uU][mM][nN][sS]")
  keyword_materialized (:pattern "[mM][aA][tT][eE][rR][iI][aA][lL][iI][zZ][eE][dD]")
  keyword_tablespace (:pattern "[tT][aA][bB][lL][eE][sS][pP][aA][cC][eE]")
  keyword_sequence (:pattern "[sS][eE][qQ][uU][eE][nN][cC][eE]")
  keyword_increment (:pattern "[iI][nN][cC][rR][eE][mM][eE][nN][tT]")
  keyword_minvalue (:pattern "[mM][iI][nN][vV][aA][lL][uU][eE]")
  keyword_maxvalue (:pattern "[mM][aA][xX][vV][aA][lL][uU][eE]")
  keyword_none (:pattern "[nN][oO][nN][eE]")
  keyword_owned (:pattern "[oO][wW][nN][eE][dD]")
  keyword_start (:pattern "[sS][tT][aA][rR][tT]")
  keyword_restart (:pattern "[rR][eE][sS][tT][aA][rR][tT]")
  keyword_key (:pattern "[kK][eE][yY]")
  keyword_duplicate (:pattern "[dD][uU][pP][lL][iI][cC][aA][tT][eE]")
  keyword_as (:pattern "[aA][sS]")
  keyword_distinct (:pattern "[dD][iI][sS][tT][iI][nN][cC][tT]")
  keyword_constraint (:pattern "[cC][oO][nN][sS][tT][rR][aA][iI][nN][tT]")
  keyword_filter (:pattern "[fF][iI][lL][tT][eE][rR]")
  keyword_cast (:pattern "[cC][aA][sS][tT]")
  keyword_separator (:pattern "[sS][eE][pP][aA][rR][aA][tT][oO][rR]")
  keyword_case (:pattern "[cC][aA][sS][eE]")
  keyword_when (:pattern "[wW][hH][eE][nN]")
  keyword_then (:pattern "[tT][hH][eE][nN]")
  keyword_else (:pattern "[eE][lL][sS][eE]")
  keyword_end (:pattern "[eE][nN][dD]")
  keyword_in (:pattern "[iI][nN]")
  keyword_and (:pattern "[aA][nN][dD]")
  keyword_or (:pattern "[oO][rR]")
  keyword_is (:pattern "[iI][sS]")
  keyword_not (:pattern "[nN][oO][tT]")
  keyword_force (:pattern "[fF][oO][rR][cC][eE]")
  keyword_ignore (:pattern "[iI][gG][nN][oO][rR][eE]")
  keyword_using (:pattern "[uU][sS][iI][nN][gG]")
  keyword_use (:pattern "[uU][sS][eE]")
  keyword_index (:pattern "[iI][nN][dD][eE][xX]")
  keyword_for (:pattern "[fF][oO][rR]")
  keyword_if (:pattern "[iI][fF]")
  keyword_exists (:pattern "[eE][xX][iI][sS][tT][sS]")
  keyword_auto_increment (:pattern "[aA][uU][tT][oO][__][iI][nN][cC][rR][eE][mM][eE][nN][tT]")
  keyword_generated (:pattern "[gG][eE][nN][eE][rR][aA][tT][eE][dD]")
  keyword_always (:pattern "[aA][lL][wW][aA][yY][sS]")
  keyword_collate (:pattern "[cC][oO][lL][lL][aA][tT][eE]")
  keyword_character (:pattern "[cC][hH][aA][rR][aA][cC][tT][eE][rR]")
  keyword_engine (:pattern "[eE][nN][gG][iI][nN][eE]")
  keyword_default (:pattern "[dD][eE][fF][aA][uU][lL][tT]")
  keyword_cascade (:pattern "[cC][aA][sS][cC][aA][dD][eE]")
  keyword_restrict (:pattern "[rR][eE][sS][tT][rR][iI][cC][tT]")
  keyword_with (:pattern "[wW][iI][tT][hH]")
  keyword_without (:pattern "[wW][iI][tT][hH][oO][uU][tT]")
  keyword_no (:pattern "[nN][oO]")
  keyword_data (:pattern "[dD][aA][tT][aA]")
  keyword_type (:pattern "[tT][yY][pP][eE]")
  keyword_rename (:pattern "[rR][eE][nN][aA][mM][eE]")
  keyword_to (:pattern "[tT][oO]")
  keyword_database (:pattern "[dD][aA][tT][aA][bB][aA][sS][eE]")
  keyword_schema (:pattern "[sS][cC][hH][eE][mM][aA]")
  keyword_owner (:pattern "[oO][wW][nN][eE][rR]")
  keyword_user (:pattern "[uU][sS][eE][rR]")
  keyword_admin (:pattern "[aA][dD][mM][iI][nN]")
  keyword_password (:pattern "[pP][aA][sS][sS][wW][oO][rR][dD]")
  keyword_encrypted (:pattern "[eE][nN][cC][rR][yY][pP][tT][eE][dD]")
  keyword_valid (:pattern "[vV][aA][lL][iI][dD]")
  keyword_until (:pattern "[uU][nN][tT][iI][lL]")
  keyword_connection (:pattern "[cC][oO][nN][nN][eE][cC][tT][iI][oO][nN]")
  keyword_role (:pattern "[rR][oO][lL][eE]")
  keyword_reset (:pattern "[rR][eE][sS][eE][tT]")
  keyword_temp (:pattern "[tT][eE][mM][pP]")
  keyword_temporary (:pattern "[tT][eE][mM][pP][oO][rR][aA][rR][yY]")
  keyword_unlogged (:pattern "[uU][nN][lL][oO][gG][gG][eE][dD]")
  keyword_logged (:pattern "[lL][oO][gG][gG][eE][dD]")
  keyword_cycle (:pattern "[cC][yY][cC][lL][eE]")
  keyword_union (:pattern "[uU][nN][iI][oO][nN]")
  keyword_all (:pattern "[aA][lL][lL]")
  keyword_any (:pattern "[aA][nN][yY]")
  keyword_some (:pattern "[sS][oO][mM][eE]")
  keyword_except (:pattern "[eE][xX][cC][eE][pP][tT]")
  keyword_intersect (:pattern "[iI][nN][tT][eE][rR][sS][eE][cC][tT]")
  keyword_returning (:pattern "[rR][eE][tT][uU][rR][nN][iI][nN][gG]")
  keyword_begin (:pattern "[bB][eE][gG][iI][nN]")
  keyword_commit (:pattern "[cC][oO][mM][mM][iI][tT]")
  keyword_rollback (:pattern "[rR][oO][lL][lL][bB][aA][cC][kK]")
  keyword_transaction (:pattern "[tT][rR][aA][nN][sS][aA][cC][tT][iI][oO][nN]")
  keyword_over (:pattern "[oO][vV][eE][rR]")
  keyword_nulls (:pattern "[nN][uU][lL][lL][sS]")
  keyword_first (:pattern "[fF][iI][rR][sS][tT]")
  keyword_after (:pattern "[aA][fF][tT][eE][rR]")
  keyword_before (:pattern "[bB][eE][fF][oO][rR][eE]")
  keyword_last (:pattern "[lL][aA][sS][tT]")
  keyword_window (:pattern "[wW][iI][nN][dD][oO][wW]")
  keyword_range (:pattern "[rR][aA][nN][gG][eE]")
  keyword_rows (:pattern "[rR][oO][wW][sS]")
  keyword_groups (:pattern "[gG][rR][oO][uU][pP][sS]")
  keyword_between (:pattern "[bB][eE][tT][wW][eE][eE][nN]")
  keyword_unbounded (:pattern "[uU][nN][bB][oO][uU][nN][dD][eE][dD]")
  keyword_preceding (:pattern "[pP][rR][eE][cC][eE][dD][iI][nN][gG]")
  keyword_following (:pattern "[fF][oO][lL][lL][oO][wW][iI][nN][gG]")
  keyword_exclude (:pattern "[eE][xX][cC][lL][uU][dD][eE]")
  keyword_current (:pattern "[cC][uU][rR][rR][eE][nN][tT]")
  keyword_row (:pattern "[rR][oO][wW]")
  keyword_ties (:pattern "[tT][iI][eE][sS]")
  keyword_others (:pattern "[oO][tT][hH][eE][rR][sS]")
  keyword_only (:pattern "[oO][nN][lL][yY]")
  keyword_unique (:pattern "[uU][nN][iI][qQ][uU][eE]")
  keyword_foreign (:pattern "[fF][oO][rR][eE][iI][gG][nN]")
  keyword_references (:pattern "[rR][eE][fF][eE][rR][eE][nN][cC][eE][sS]")
  keyword_concurrently (:pattern "[cC][oO][nN][cC][uU][rR][rR][eE][nN][tT][lL][yY]")
  keyword_btree (:pattern "[bB][tT][rR][eE][eE]")
  keyword_hash (:pattern "[hH][aA][sS][hH]")
  keyword_gist (:pattern "[gG][iI][sS][tT]")
  keyword_spgist (:pattern "[sS][pP][gG][iI][sS][tT]")
  keyword_gin (:pattern "[gG][iI][nN]")
  keyword_brin (:pattern "[bB][rR][iI][nN]")
  keyword_like (:choice (:pattern "[lL][iI][kK][eE]") (:pattern "[iI][lL][iI][kK][eE]"))
  keyword_similar (:pattern "[sS][iI][mM][iI][lL][aA][rR]")
  keyword_unsigned (:pattern "[uU][nN][sS][iI][gG][nN][eE][dD]")
  keyword_zerofill (:pattern "[zZ][eE][rR][oO][fF][iI][lL][lL]")
  keyword_conflict (:pattern "[cC][oO][nN][fF][lL][iI][cC][tT]")
  keyword_do (:pattern "[dD][oO]")
  keyword_nothing (:pattern "[nN][oO][tT][hH][iI][nN][gG]")
  keyword_high_priority (:pattern "[hH][iI][gG][hH][__][pP][rR][iI][oO][rR][iI][tT][yY]")
  keyword_low_priority (:pattern "[lL][oO][wW][__][pP][rR][iI][oO][rR][iI][tT][yY]")
  keyword_delayed (:pattern "[dD][eE][lL][aA][yY][eE][dD]")
  keyword_recursive (:pattern "[rR][eE][cC][uU][rR][sS][iI][vV][eE]")
  keyword_cascaded (:pattern "[cC][aA][sS][cC][aA][dD][eE][dD]")
  keyword_local (:pattern "[lL][oO][cC][aA][lL]")
  keyword_current_timestamp (:pattern "[cC][uU][rR][rR][eE][nN][tT][__][tT][iI][mM][eE][sS][tT][aA][mM][pP]")
  keyword_check (:pattern "[cC][hH][eE][cC][kK]")
  keyword_option (:pattern "[oO][pP][tT][iI][oO][nN]")
  keyword_vacuum (:pattern "[vV][aA][cC][uU][uU][mM]")
  keyword_wait (:pattern "[wW][aA][iI][tT]")
  keyword_nowait (:pattern "[nN][oO][wW][aA][iI][tT]")
  keyword_attribute (:pattern "[aA][tT][tT][rR][iI][bB][uU][tT][eE]")
  keyword_authorization (:pattern "[aA][uU][tT][hH][oO][rR][iI][zZ][aA][tT][iI][oO][nN]")
  keyword_action (:pattern "[aA][cC][tT][iI][oO][nN]")
  keyword_extension (:pattern "[eE][xX][tT][eE][nN][sS][iI][oO][nN]")
  keyword_copy (:pattern "[cC][oO][pP][yY]")
  keyword_stdin (:pattern "[sS][tT][dD][iI][nN]")
  keyword_freeze (:pattern "[fF][rR][eE][eE][zZ][eE]")
  keyword_escape (:pattern "[eE][sS][cC][aA][pP][eE]")
  keyword_encoding (:pattern "[eE][nN][cC][oO][dD][iI][nN][gG]")
  keyword_force_quote (:pattern "[fF][oO][rR][cC][eE][__][qQ][uU][oO][tT][eE]")
  keyword_quote (:pattern "[qQ][uU][oO][tT][eE]")
  keyword_force_null (:pattern "[fF][oO][rR][cC][eE][__][nN][uU][lL][lL]")
  keyword_force_not_null (:pattern "[fF][oO][rR][cC][eE][__][nN][oO][tT][__][nN][uU][lL][lL]")
  keyword_header (:pattern "[hH][eE][aA][dD][eE][rR]")
  keyword_match (:pattern "[mM][aA][tT][cC][hH]")
  keyword_program (:pattern "[pP][rR][oO][gG][rR][aA][mM]")
  keyword_plain (:pattern "[pP][lL][aA][iI][nN]")
  keyword_extended (:pattern "[eE][xX][tT][eE][nN][dD][eE][dD]")
  keyword_main (:pattern "[mM][aA][iI][nN]")
  keyword_storage (:pattern "[sS][tT][oO][rR][aA][gG][eE]")
  keyword_compression (:pattern "[cC][oO][mM][pP][rR][eE][sS][sS][iI][oO][nN]")
  keyword_trigger (:pattern "[tT][rR][iI][gG][gG][eE][rR]")
  keyword_function (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")
  keyword_returns (:pattern "[rR][eE][tT][uU][rR][nN][sS]")
  keyword_return (:pattern "[rR][eE][tT][uU][rR][nN]")
  keyword_setof (:pattern "[sS][eE][tT][oO][fF]")
  keyword_atomic (:pattern "[aA][tT][oO][mM][iI][cC]")
  keyword_declare (:pattern "[dD][eE][cC][lL][aA][rR][eE]")
  keyword_language (:pattern "[lL][aA][nN][gG][uU][aA][gG][eE]")
  keyword_immutable (:pattern "[iI][mM][mM][uU][tT][aA][bB][lL][eE]")
  keyword_stable (:pattern "[sS][tT][aA][bB][lL][eE]")
  keyword_volatile (:pattern "[vV][oO][lL][aA][tT][iI][lL][eE]")
  keyword_leakproof (:pattern "[lL][eE][aA][kK][pP][rR][oO][oO][fF]")
  keyword_parallel (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL]")
  keyword_safe (:pattern "[sS][aA][fF][eE]")
  keyword_unsafe (:pattern "[uU][nN][sS][aA][fF][eE]")
  keyword_restricted (:pattern "[rR][eE][sS][tT][rR][iI][cC][tT][eE][dD]")
  keyword_called (:pattern "[cC][aA][lL][lL][eE][dD]")
  keyword_input (:pattern "[iI][nN][pP][uU][tT]")
  keyword_strict (:pattern "[sS][tT][rR][iI][cC][tT]")
  keyword_cost (:pattern "[cC][oO][sS][tT]")
  keyword_support (:pattern "[sS][uU][pP][pP][oO][rR][tT]")
  keyword_definer (:pattern "[dD][eE][fF][iI][nN][eE][rR]")
  keyword_invoker (:pattern "[iI][nN][vV][oO][kK][eE][rR]")
  keyword_security (:pattern "[sS][eE][cC][uU][rR][iI][tT][yY]")
  keyword_version (:pattern "[vV][eE][rR][sS][iI][oO][nN]")
  keyword_out (:pattern "[oO][uU][tT]")
  keyword_inout (:pattern "[iI][nN][oO][uU][tT]")
  keyword_variadic (:pattern "[vV][aA][rR][iI][aA][dD][iI][cC]")
  keyword_ordinality (:pattern "[oO][rR][dD][iI][nN][aA][lL][iI][tT][yY]")
  keyword_session (:pattern "[sS][eE][sS][sS][iI][oO][nN]")
  keyword_isolation (:pattern "[iI][sS][oO][lL][aA][tT][iI][oO][nN]")
  keyword_level (:pattern "[lL][eE][vV][eE][lL]")
  keyword_serializable (:pattern "[sS][eE][rR][iI][aA][lL][iI][zZ][aA][bB][lL][eE]")
  keyword_repeatable (:pattern "[rR][eE][pP][eE][aA][tT][aA][bB][lL][eE]")
  keyword_read (:pattern "[rR][eE][aA][dD]")
  keyword_write (:pattern "[wW][rR][iI][tT][eE]")
  keyword_committed (:pattern "[cC][oO][mM][mM][iI][tT][tT][eE][dD]")
  keyword_uncommitted (:pattern "[uU][nN][cC][oO][mM][mM][iI][tT][tT][eE][dD]")
  keyword_deferrable (:pattern "[dD][eE][fF][eE][rR][rR][aA][bB][lL][eE]")
  keyword_names (:pattern "[nN][aA][mM][eE][sS]")
  keyword_zone (:pattern "[zZ][oO][nN][eE]")
  keyword_immediate (:pattern "[iI][mM][mM][eE][dD][iI][aA][tT][eE]")
  keyword_deferred (:pattern "[dD][eE][fF][eE][rR][rR][eE][dD]")
  keyword_constraints (:pattern "[cC][oO][nN][sS][tT][rR][aA][iI][nN][tT][sS]")
  keyword_snapshot (:pattern "[sS][nN][aA][pP][sS][hH][oO][tT]")
  keyword_characteristics (:pattern "[cC][hH][aA][rR][aA][cC][tT][eE][rR][iI][sS][tT][iI][cC][sS]")
  keyword_follows (:pattern "[fF][oO][lL][lL][oO][wW][sS]")
  keyword_precedes (:pattern "[pP][rR][eE][cC][eE][dD][eE][sS]")
  keyword_each (:pattern "[eE][aA][cC][hH]")
  keyword_instead (:pattern "[iI][nN][sS][tT][eE][aA][dD]")
  keyword_of (:pattern "[oO][fF]")
  keyword_initially (:pattern "[iI][nN][iI][tT][iI][aA][lL][lL][yY]")
  keyword_old (:pattern "[oO][lL][dD]")
  keyword_new (:pattern "[nN][eE][wW]")
  keyword_referencing (:pattern "[rR][eE][fF][eE][rR][eE][nN][cC][iI][nN][gG]")
  keyword_statement (:pattern "[sS][tT][aA][tT][eE][mM][eE][nN][tT]")
  keyword_execute (:pattern "[eE][xX][eE][cC][uU][tT][eE]")
  keyword_procedure (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")
  keyword_object_id (:pattern "[oO][bB][jJ][eE][cC][tT][__][iI][dD]")
  keyword_external (:pattern "[eE][xX][tT][eE][rR][nN][aA][lL]")
  keyword_stored (:pattern "[sS][tT][oO][rR][eE][dD]")
  keyword_virtual (:pattern "[vV][iI][rR][tT][uU][aA][lL]")
  keyword_cached (:pattern "[cC][aA][cC][hH][eE][dD]")
  keyword_uncached (:pattern "[uU][nN][cC][aA][cC][hH][eE][dD]")
  keyword_replication (:pattern "[rR][eE][pP][lL][iI][cC][aA][tT][iI][oO][nN]")
  keyword_tblproperties (:pattern "[tT][bB][lL][pP][rR][oO][pP][eE][rR][tT][iI][eE][sS]")
  keyword_compute (:pattern "[cC][oO][mM][pP][uU][tT][eE]")
  keyword_stats (:pattern "[sS][tT][aA][tT][sS]")
  keyword_statistics (:pattern "[sS][tT][aA][tT][iI][sS][tT][iI][cC][sS]")
  keyword_optimize (:pattern "[oO][pP][tT][iI][mM][iI][zZ][eE]")
  keyword_rewrite (:pattern "[rR][eE][wW][rR][iI][tT][eE]")
  keyword_bin_pack (:pattern "[bB][iI][nN][__][pP][aA][cC][kK]")
  keyword_incremental (:pattern "[iI][nN][cC][rR][eE][mM][eE][nN][tT][aA][lL]")
  keyword_location (:pattern "[lL][oO][cC][aA][tT][iI][oO][nN]")
  keyword_partitioned (:pattern "[pP][aA][rR][tT][iI][tT][iI][oO][nN][eE][dD]")
  keyword_comment (:pattern "[cC][oO][mM][mM][eE][nN][tT]")
  keyword_sort (:pattern "[sS][oO][rR][tT]")
  keyword_format (:pattern "[fF][oO][rR][mM][aA][tT]")
  keyword_delimited (:pattern "[dD][eE][lL][iI][mM][iI][tT][eE][dD]")
  keyword_delimiter (:pattern "[dD][eE][lL][iI][mM][iI][tT][eE][rR]")
  keyword_fields (:pattern "[fF][iI][eE][lL][dD][sS]")
  keyword_terminated (:pattern "[tT][eE][rR][mM][iI][nN][aA][tT][eE][dD]")
  keyword_escaped (:pattern "[eE][sS][cC][aA][pP][eE][dD]")
  keyword_lines (:pattern "[lL][iI][nN][eE][sS]")
  keyword_cache (:pattern "[cC][aA][cC][hH][eE]")
  keyword_metadata (:pattern "[mM][eE][tT][aA][dD][aA][tT][aA]")
  keyword_noscan (:pattern "[nN][oO][sS][cC][aA][nN]")
  keyword_parquet (:pattern "[pP][aA][rR][qQ][uU][eE][tT]")
  keyword_rcfile (:pattern "[rR][cC][fF][iI][lL][eE]")
  keyword_csv (:pattern "[cC][sS][vV]")
  keyword_textfile (:pattern "[tT][eE][xX][tT][fF][iI][lL][eE]")
  keyword_avro (:pattern "[aA][vV][rR][oO]")
  keyword_sequencefile (:pattern "[sS][eE][qQ][uU][eE][nN][cC][eE][fF][iI][lL][eE]")
  keyword_orc (:pattern "[oO][rR][cC]")
  keyword_jsonfile (:pattern "[jJ][sS][oO][nN][fF][iI][lL][eE]")
  is_not (:prec-left 0 (:seq keyword_is keyword_not))
  not_like (:seq keyword_not keyword_like)
  similar_to (:seq keyword_similar keyword_to)
  not_similar_to (:seq keyword_not keyword_similar keyword_to)
  distinct_from (:seq keyword_is keyword_distinct keyword_from)
  not_distinct_from (:seq keyword_is keyword_not keyword_distinct keyword_from)
  _temporary (:choice keyword_temp keyword_temporary)
  _not_null (:seq keyword_not keyword_null)
  _primary_key (:seq keyword_primary keyword_key)
  _if_exists (:seq keyword_if keyword_exists)
  _if_not_exists (:seq keyword_if keyword_not keyword_exists)
  _or_replace (:seq keyword_or keyword_replace)
  _default_null (:seq keyword_default keyword_null)
  _current_row (:seq keyword_current keyword_row)
  _exclude_current_row (:seq keyword_exclude keyword_current keyword_row)
  _exclude_group (:seq keyword_exclude keyword_group)
  _exclude_no_others (:seq keyword_exclude keyword_no keyword_others)
  _exclude_ties (:seq keyword_exclude keyword_ties)
  _check_option (:seq keyword_check keyword_option)
  direction (:choice keyword_desc keyword_asc)
  keyword_null (:pattern "[nN][uU][lL][lL]")
  keyword_true (:pattern "[tT][rR][uU][eE]")
  keyword_false (:pattern "[fF][aA][lL][sS][eE]")
  keyword_boolean (:pattern "[bB][oO][oO][lL][eE][aA][nN]")
  keyword_bit (:pattern "[bB][iI][tT]")
  keyword_binary (:pattern "[bB][iI][nN][aA][rR][yY]")
  keyword_varbinary (:pattern "[vV][aA][rR][bB][iI][nN][aA][rR][yY]")
  keyword_image (:pattern "[iI][mM][aA][gG][eE]")
  keyword_smallserial (:choice
                       (:pattern "[sS][mM][aA][lL][lL][sS][eE][rR][iI][aA][lL]")
                       (:pattern "[sS][eE][rR][iI][aA][lL][22]"))
  keyword_serial (:choice
                  (:pattern "[sS][eE][rR][iI][aA][lL]")
                  (:pattern "[sS][eE][rR][iI][aA][lL][44]"))
  keyword_bigserial (:choice
                     (:pattern "[bB][iI][gG][sS][eE][rR][iI][aA][lL]")
                     (:pattern "[sS][eE][rR][iI][aA][lL][88]"))
  keyword_tinyint (:choice (:pattern "[tT][iI][nN][yY][iI][nN][tT]") (:pattern "[iI][nN][tT][11]"))
  keyword_smallint (:choice
                    (:pattern "[sS][mM][aA][lL][lL][iI][nN][tT]")
                    (:pattern "[iI][nN][tT][22]"))
  keyword_mediumint (:choice
                     (:pattern "[mM][eE][dD][iI][uU][mM][iI][nN][tT]")
                     (:pattern "[iI][nN][tT][33]"))
  keyword_int (:choice
               (:pattern "[iI][nN][tT]")
               (:pattern "[iI][nN][tT][eE][gG][eE][rR]")
               (:pattern "[iI][nN][tT][44]"))
  keyword_bigint (:choice (:pattern "[bB][iI][gG][iI][nN][tT]") (:pattern "[iI][nN][tT][88]"))
  keyword_decimal (:pattern "[dD][eE][cC][iI][mM][aA][lL]")
  keyword_numeric (:pattern "[nN][uU][mM][eE][rR][iI][cC]")
  keyword_real (:choice (:pattern "[rR][eE][aA][lL]") (:pattern "[fF][lL][oO][aA][tT][44]"))
  keyword_float (:pattern "[fF][lL][oO][aA][tT]")
  keyword_double (:pattern "[dD][oO][uU][bB][lL][eE]")
  keyword_precision (:pattern "[pP][rR][eE][cC][iI][sS][iI][oO][nN]")
  keyword_inet (:pattern "[iI][nN][eE][tT]")
  keyword_money (:pattern "[mM][oO][nN][eE][yY]")
  keyword_smallmoney (:pattern "[sS][mM][aA][lL][lL][mM][oO][nN][eE][yY]")
  keyword_varying (:pattern "[vV][aA][rR][yY][iI][nN][gG]")
  keyword_char (:choice
                (:pattern "[cC][hH][aA][rR]")
                (:pattern "[cC][hH][aA][rR][aA][cC][tT][eE][rR]"))
  keyword_nchar (:pattern "[nN][cC][hH][aA][rR]")
  keyword_varchar (:choice
                   (:pattern "[vV][aA][rR][cC][hH][aA][rR]")
                   (:seq (:pattern "[cC][hH][aA][rR][aA][cC][tT][eE][rR]") keyword_varying))
  keyword_nvarchar (:pattern "[nN][vV][aA][rR][cC][hH][aA][rR]")
  keyword_text (:pattern "[tT][eE][xX][tT]")
  keyword_string (:pattern "[sS][tT][rR][iI][nN][gG]")
  keyword_uuid (:pattern "[uU][uU][iI][dD]")
  keyword_json (:pattern "[jJ][sS][oO][nN]")
  keyword_jsonb (:pattern "[jJ][sS][oO][nN][bB]")
  keyword_xml (:pattern "[xX][mM][lL]")
  keyword_bytea (:pattern "[bB][yY][tT][eE][aA]")
  keyword_enum (:pattern "[eE][nN][uU][mM]")
  keyword_date (:pattern "[dD][aA][tT][eE]")
  keyword_datetime (:pattern "[dD][aA][tT][eE][tT][iI][mM][eE]")
  keyword_datetime2 (:pattern "[dD][aA][tT][eE][tT][iI][mM][eE][22]")
  keyword_smalldatetime (:pattern "[sS][mM][aA][lL][lL][dD][aA][tT][eE][tT][iI][mM][eE]")
  keyword_datetimeoffset (:pattern "[dD][aA][tT][eE][tT][iI][mM][eE][oO][fF][fF][sS][eE][tT]")
  keyword_time (:pattern "[tT][iI][mM][eE]")
  keyword_timestamp (:pattern "[tT][iI][mM][eE][sS][tT][aA][mM][pP]")
  keyword_timestamptz (:pattern "[tT][iI][mM][eE][sS][tT][aA][mM][pP][tT][zZ]")
  keyword_interval (:pattern "[iI][nN][tT][eE][rR][vV][aA][lL]")
  keyword_geometry (:pattern "[gG][eE][oO][mM][eE][tT][rR][yY]")
  keyword_geography (:pattern "[gG][eE][oO][gG][rR][aA][pP][hH][yY]")
  keyword_box2d (:pattern "[bB][oO][xX][22][dD]")
  keyword_box3d (:pattern "[bB][oO][xX][33][dD]")
  keyword_oid (:pattern "[oO][iI][dD]")
  keyword_oids (:pattern "[oO][iI][dD][sS]")
  keyword_name (:pattern "[nN][aA][mM][eE]")
  keyword_regclass (:pattern "[rR][eE][gG][cC][lL][aA][sS][sS]")
  keyword_regnamespace (:pattern "[rR][eE][gG][nN][aA][mM][eE][sS][pP][aA][cC][eE]")
  keyword_regproc (:pattern "[rR][eE][gG][pP][rR][oO][cC]")
  keyword_regtype (:pattern "[rR][eE][gG][tT][yY][pP][eE]")
  keyword_array (:pattern "[aA][rR][rR][aA][yY]")
  _type (:prec-left 0
         (:seq
          (:choice
           keyword_boolean
           bit
           binary
           varbinary
           keyword_image
           keyword_smallserial
           keyword_serial
           keyword_bigserial
           tinyint
           smallint
           mediumint
           int
           bigint
           decimal
           numeric
           double
           float
           keyword_money
           keyword_smallmoney
           char
           varchar
           nchar
           nvarchar
           numeric
           keyword_string
           keyword_text
           keyword_uuid
           keyword_json
           keyword_jsonb
           keyword_xml
           keyword_bytea
           keyword_inet
           enum
           keyword_date
           keyword_datetime
           keyword_datetime2
           datetimeoffset
           keyword_smalldatetime
           time
           timestamp
           keyword_timestamptz
           keyword_interval
           keyword_geometry
           keyword_geography
           keyword_box2d
           keyword_box3d
           keyword_oid
           keyword_name
           keyword_regclass
           keyword_regnamespace
           keyword_regproc
           keyword_regtype
           (:field :custom_type object_reference))
          (:choice array_size_definition :blank)))
  array_size_definition (:prec-left 0
                         (:choice
                          (:seq keyword_array (:choice _array_size_definition :blank))
                          (:repeat1 _array_size_definition)))
  _array_size_definition (:seq "[" (:choice (:field :size (:alias _integer literal)) :blank) "]")
  tinyint (:choice
           (:seq
            keyword_unsigned
            (:prec-right 1
             (:choice
              keyword_tinyint
              (:seq
               keyword_tinyint
               (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")")))))
           (:seq
            (:prec-right 1
             (:choice
              keyword_tinyint
              (:seq
               keyword_tinyint
               (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
            (:choice keyword_unsigned :blank)
            (:choice keyword_zerofill :blank)))
  smallint (:choice
            (:seq
             keyword_unsigned
             (:prec-right 1
              (:choice
               keyword_smallint
               (:seq
                keyword_smallint
                (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")")))))
            (:seq
             (:prec-right 1
              (:choice
               keyword_smallint
               (:seq
                keyword_smallint
                (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
             (:choice keyword_unsigned :blank)
             (:choice keyword_zerofill :blank)))
  mediumint (:choice
             (:seq
              keyword_unsigned
              (:prec-right 1
               (:choice
                keyword_mediumint
                (:seq
                 keyword_mediumint
                 (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")")))))
             (:seq
              (:prec-right 1
               (:choice
                keyword_mediumint
                (:seq
                 keyword_mediumint
                 (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
              (:choice keyword_unsigned :blank)
              (:choice keyword_zerofill :blank)))
  int (:choice
       (:seq
        keyword_unsigned
        (:prec-right 1
         (:choice
          keyword_int
          (:seq keyword_int (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")")))))
       (:seq
        (:prec-right 1
         (:choice
          keyword_int
          (:seq keyword_int (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
        (:choice keyword_unsigned :blank)
        (:choice keyword_zerofill :blank)))
  bigint (:choice
          (:seq
           keyword_unsigned
           (:prec-right 1
            (:choice
             keyword_bigint
             (:seq
              keyword_bigint
              (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")")))))
          (:seq
           (:prec-right 1
            (:choice
             keyword_bigint
             (:seq
              keyword_bigint
              (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
           (:choice keyword_unsigned :blank)
           (:choice keyword_zerofill :blank)))
  bit (:choice
       keyword_bit
       (:seq
        keyword_bit
        (:prec 0
         (:prec-right 1
          (:choice
           keyword_varying
           (:seq
            keyword_varying
            (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))))
       (:prec 1
        (:prec-right 1
         (:choice
          keyword_bit
          (:seq
           keyword_bit
           (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))))
  binary (:prec-right 1
          (:choice
           keyword_binary
           (:seq
            keyword_binary
            (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))
  varbinary (:prec-right 1
             (:choice
              keyword_varbinary
              (:seq
               keyword_varbinary
               (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))
  float (:choice
         (:prec-right 1
          (:choice
           keyword_float
           (:seq
            keyword_float
            (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))
         (:choice
          (:seq
           keyword_unsigned
           (:prec-right 1
            (:choice
             keyword_float
             (:seq
              keyword_float
              (:seq
               "("
               (:seq
                (:field :precision (:alias _natural_number literal))
                (:seq "," (:field :scale (:alias _natural_number literal))))
               ")")))))
          (:seq
           (:prec-right 1
            (:choice
             keyword_float
             (:seq
              keyword_float
              (:seq
               "("
               (:seq
                (:field :precision (:alias _natural_number literal))
                (:seq "," (:field :scale (:alias _natural_number literal))))
               ")"))))
           (:choice keyword_unsigned :blank)
           (:choice keyword_zerofill :blank))))
  double (:choice
          (:pattern "[fF][lL][oO][aA][tT][88]")
          (:choice
           (:seq
            keyword_unsigned
            (:prec-right 1
             (:choice
              keyword_double
              (:seq
               keyword_double
               (:seq
                "("
                (:seq
                 (:field :precision (:alias _natural_number literal))
                 (:seq "," (:field :scale (:alias _natural_number literal))))
                ")")))))
           (:seq
            (:prec-right 1
             (:choice
              keyword_double
              (:seq
               keyword_double
               (:seq
                "("
                (:seq
                 (:field :precision (:alias _natural_number literal))
                 (:seq "," (:field :scale (:alias _natural_number literal))))
                ")"))))
            (:choice keyword_unsigned :blank)
            (:choice keyword_zerofill :blank)))
          (:choice
           (:seq
            keyword_unsigned
            (:prec-right 1
             (:choice
              (:seq keyword_double keyword_precision)
              (:seq
               (:seq keyword_double keyword_precision)
               (:seq
                "("
                (:seq
                 (:field :precision (:alias _natural_number literal))
                 (:seq "," (:field :scale (:alias _natural_number literal))))
                ")")))))
           (:seq
            (:prec-right 1
             (:choice
              (:seq keyword_double keyword_precision)
              (:seq
               (:seq keyword_double keyword_precision)
               (:seq
                "("
                (:seq
                 (:field :precision (:alias _natural_number literal))
                 (:seq "," (:field :scale (:alias _natural_number literal))))
                ")"))))
            (:choice keyword_unsigned :blank)
            (:choice keyword_zerofill :blank)))
          (:choice
           (:seq
            keyword_unsigned
            (:prec-right 1
             (:choice
              keyword_real
              (:seq
               keyword_real
               (:seq
                "("
                (:seq
                 (:field :precision (:alias _natural_number literal))
                 (:seq "," (:field :scale (:alias _natural_number literal))))
                ")")))))
           (:seq
            (:prec-right 1
             (:choice
              keyword_real
              (:seq
               keyword_real
               (:seq
                "("
                (:seq
                 (:field :precision (:alias _natural_number literal))
                 (:seq "," (:field :scale (:alias _natural_number literal))))
                ")"))))
            (:choice keyword_unsigned :blank)
            (:choice keyword_zerofill :blank))))
  decimal (:choice
           (:prec-right 1
            (:choice
             keyword_decimal
             (:seq
              keyword_decimal
              (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))
           (:prec-right 1
            (:choice
             keyword_decimal
             (:seq
              keyword_decimal
              (:seq
               "("
               (:seq
                (:field :precision (:alias _natural_number literal))
                (:seq "," (:field :scale (:alias _natural_number literal))))
               ")")))))
  numeric (:choice
           (:prec-right 1
            (:choice
             keyword_numeric
             (:seq
              keyword_numeric
              (:seq "(" (:seq (:field :precision (:alias _natural_number literal))) ")"))))
           (:prec-right 1
            (:choice
             keyword_numeric
             (:seq
              keyword_numeric
              (:seq
               "("
               (:seq
                (:field :precision (:alias _natural_number literal))
                (:seq "," (:field :scale (:alias _natural_number literal))))
               ")")))))
  char (:prec-right 1
        (:choice
         keyword_char
         (:seq keyword_char (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
  varchar (:prec-right 1
           (:choice
            keyword_varchar
            (:seq
             keyword_varchar
             (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
  nchar (:prec-right 1
         (:choice
          keyword_nchar
          (:seq keyword_nchar (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
  nvarchar (:prec-right 1
            (:choice
             keyword_nvarchar
             (:seq
              keyword_nvarchar
              (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
  _include_time_zone (:seq (:choice keyword_with keyword_without) keyword_time keyword_zone)
  datetimeoffset (:prec-right 1
                  (:choice
                   keyword_datetimeoffset
                   (:seq
                    keyword_datetimeoffset
                    (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
  time (:seq
        (:prec-right 1
         (:choice
          keyword_time
          (:seq keyword_time (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
        (:choice _include_time_zone :blank))
  timestamp (:seq
             (:prec-right 1
              (:choice
               keyword_timestamp
               (:seq
                keyword_timestamp
                (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
             (:choice _include_time_zone :blank))
  timestamptz (:prec-right 1
               (:choice
                keyword_timestamptz
                (:seq
                 keyword_timestamptz
                 (:seq "(" (:seq (:field :size (:alias _natural_number literal))) ")"))))
  enum (:seq
        keyword_enum
        (:seq
         "("
         (:seq
          (:field :value (:alias _literal_string literal))
          (:repeat (:seq "," (:field :value (:alias _literal_string literal)))))
         ")"))
  array (:seq
         keyword_array
         (:choice
          (:seq "[" (:choice (:seq _expression (:repeat (:seq "," _expression))) :blank) "]")
          (:seq "(" _dml_read ")")))
  comment (:pattern "--.*")
  marginalia (:pattern "\\/\\*[^*]*\\*+(?:[^/*][^*]*\\*+)*\\/")
  transaction (:seq
               keyword_begin
               (:choice keyword_transaction :blank)
               (:choice ";" :blank)
               (:repeat (:seq statement ";"))
               (:choice _commit _rollback))
  _commit (:seq keyword_commit (:choice keyword_transaction :blank))
  _rollback (:seq keyword_rollback (:choice keyword_transaction :blank))
  block (:seq keyword_begin (:choice ";" :blank) (:repeat (:seq statement ";")) keyword_end)
  statement (:seq
             (:choice
              (:seq
               keyword_explain
               (:choice keyword_analyze :blank)
               (:choice keyword_verbose :blank))
              :blank)
             (:choice
              _ddl_statement
              _dml_write
              (:prec-right 0 (:choice _dml_read (:seq "(" _dml_read ")")))))
  _ddl_statement (:choice
                  _create_statement
                  _alter_statement
                  _drop_statement
                  _rename_statement
                  _optimize_statement
                  _merge_statement
                  comment_statement
                  set_statement
                  reset_statement)
  _cte (:seq keyword_with (:choice keyword_recursive :blank) cte (:repeat (:seq "," cte)))
  _dml_write (:seq
              (:seq
               (:choice _cte :blank)
               (:choice
                _delete_statement
                _insert_statement
                _update_statement
                _truncate_statement
                _copy_statement)))
  _dml_read (:seq
             (:choice (:prec-right 0 (:choice _cte (:seq "(" _cte ")"))) :blank)
             (:prec-right 0
              (:choice
               (:choice _select_statement set_operation _show_statement _unload_statement)
               (:seq
                "("
                (:choice _select_statement set_operation _show_statement _unload_statement)
                ")"))))
  _unload_statement (:seq
                     keyword_unload
                     (:seq "(" _select_statement ")")
                     keyword_to
                     _single_quote_string
                     storage_parameters)
  _show_statement (:seq keyword_show (:choice _show_create keyword_all _show_tables))
  _show_tables (:seq
                keyword_tables
                (:choice (:seq keyword_from _qualified_field) :blank)
                (:choice (:seq keyword_like _expression) :blank))
  _show_create (:seq
                keyword_create
                (:choice
                 keyword_schema
                 keyword_table
                 (:seq (:choice keyword_materialized :blank) keyword_view)
                 keyword_user
                 keyword_trigger
                 keyword_procedure
                 keyword_function)
                object_reference)
  cte (:seq
       identifier
       (:choice
        (:seq
         "("
         (:choice
          (:seq (:field :argument identifier) (:repeat (:seq "," (:field :argument identifier))))
          :blank)
         ")")
        :blank)
       keyword_as
       (:choice (:seq (:choice keyword_not :blank) keyword_materialized) :blank)
       (:seq "(" (:alias (:choice _dml_read _dml_write) statement) ")"))
  set_operation (:seq
                 _select_statement
                 (:repeat1
                  (:seq
                   (:field :operation
                    (:choice
                     (:seq keyword_union (:choice keyword_all :blank))
                     keyword_except
                     keyword_intersect))
                   _select_statement)))
  _select_statement (:prec-right 0
                     (:choice
                      (:seq
                       select
                       (:choice (:seq keyword_into select_expression) :blank)
                       (:choice from :blank))
                      (:seq
                       "("
                       (:seq
                        select
                        (:choice (:seq keyword_into select_expression) :blank)
                        (:choice from :blank))
                       ")")))
  comment_statement (:seq
                     keyword_comment
                     keyword_on
                     _comment_target
                     keyword_is
                     (:choice keyword_null (:alias _literal_string literal)))
  _argmode (:choice
            keyword_in
            keyword_out
            keyword_inout
            keyword_variadic
            (:seq keyword_in keyword_out))
  function_argument (:seq
                     (:choice _argmode :blank)
                     (:choice identifier :blank)
                     _type
                     (:choice (:seq (:choice keyword_default "=") literal) :blank))
  function_arguments (:seq
                      "("
                      (:choice
                       (:seq function_argument (:repeat (:seq "," function_argument)))
                       :blank)
                      ")")
  _comment_target (:choice
                   cast
                   (:seq keyword_column (:alias _qualified_field object_reference))
                   (:seq keyword_database identifier)
                   (:seq keyword_extension object_reference)
                   (:seq keyword_function object_reference (:choice function_arguments :blank))
                   (:seq keyword_index object_reference)
                   (:seq keyword_materialized keyword_view object_reference)
                   (:seq keyword_role identifier)
                   (:seq keyword_schema identifier)
                   (:seq keyword_sequence object_reference)
                   (:seq keyword_table object_reference)
                   (:seq keyword_tablespace identifier)
                   (:seq keyword_trigger identifier keyword_on object_reference)
                   (:seq keyword_type identifier)
                   (:seq keyword_view object_reference))
  select (:seq keyword_select (:seq (:choice keyword_distinct :blank) select_expression))
  select_expression (:seq term (:repeat (:seq "," term)))
  term (:seq (:field :value (:choice all_fields _expression)) (:choice _alias :blank))
  _truncate_statement (:seq
                       keyword_truncate
                       (:choice keyword_table :blank)
                       (:choice keyword_only :blank)
                       (:choice
                        (:seq object_reference (:repeat (:seq "," object_reference)))
                        :blank)
                       (:choice _drop_behavior :blank))
  _delete_statement (:seq delete (:alias _delete_from from) (:choice returning :blank))
  _delete_from (:seq
                keyword_from
                (:choice keyword_only :blank)
                object_reference
                (:choice where :blank)
                (:choice order_by :blank)
                (:choice limit :blank))
  delete (:seq keyword_delete (:choice index_hint :blank))
  _create_statement (:seq
                     (:choice
                      create_table
                      create_view
                      create_materialized_view
                      create_index
                      create_function
                      create_type
                      create_database
                      create_role
                      create_sequence
                      create_extension
                      create_trigger
                      (:prec-left 0 (:seq create_schema (:repeat _create_statement)))))
  _table_settings (:choice
                   table_partition
                   stored_as
                   storage_location
                   table_sort
                   row_format
                   (:seq
                    keyword_tblproperties
                    (:seq "(" (:seq table_option (:repeat (:seq "," table_option))) ")"))
                   (:seq keyword_without keyword_oids)
                   storage_parameters
                   table_option)
  storage_parameters (:seq
                      keyword_with
                      (:seq
                       "("
                       (:seq
                        (:seq identifier (:choice (:seq "=" (:choice literal array)) :blank))
                        (:repeat
                         (:seq
                          ","
                          (:seq identifier (:choice (:seq "=" (:choice literal array)) :blank)))))
                       ")"))
  create_table (:prec-left 0
                (:seq
                 keyword_create
                 (:choice (:choice _temporary keyword_unlogged keyword_external) :blank)
                 keyword_table
                 (:choice _if_not_exists :blank)
                 object_reference
                 (:choice
                  (:seq
                   column_definitions
                   (:repeat _table_settings)
                   (:choice (:seq keyword_as _select_statement) :blank))
                  (:seq (:repeat _table_settings) (:seq keyword_as create_query)))))
  reset_statement (:seq
                   keyword_reset
                   (:choice
                    object_reference
                    keyword_all
                    (:seq keyword_session keyword_authorization)
                    keyword_role))
  _transaction_mode (:seq
                     keyword_isolation
                     keyword_level
                     (:choice
                      keyword_serializable
                      (:seq keyword_repeatable keyword_read)
                      (:seq keyword_read keyword_committed)
                      (:seq keyword_read keyword_uncommitted))
                     (:choice (:seq keyword_read keyword_write) (:seq keyword_read keyword_only))
                     (:choice keyword_not :blank)
                     keyword_deferrable)
  set_statement (:seq
                 keyword_set
                 (:choice
                  (:seq
                   (:choice (:choice keyword_session keyword_local) :blank)
                   (:choice
                    (:seq
                     object_reference
                     (:choice keyword_to "=")
                     (:choice literal keyword_default identifier keyword_on keyword_off))
                    (:seq keyword_schema literal)
                    (:seq keyword_names literal)
                    (:seq keyword_time keyword_zone (:choice literal keyword_local keyword_default))
                    (:seq
                     keyword_session
                     keyword_authorization
                     (:choice identifier keyword_default))
                    (:seq keyword_role (:choice identifier keyword_none))))
                  (:seq
                   keyword_constraints
                   (:choice keyword_all (:seq identifier (:repeat (:seq "," identifier))))
                   (:choice keyword_deferred keyword_immediate))
                  (:seq keyword_transaction _transaction_mode)
                  (:seq keyword_transaction keyword_snapshot _transaction_mode)
                  (:seq
                   keyword_session
                   keyword_characteristics
                   keyword_as
                   keyword_transaction
                   _transaction_mode)))
  create_query _dml_read
  create_view (:prec-right 0
               (:seq
                keyword_create
                (:choice _or_replace :blank)
                (:choice _temporary :blank)
                (:choice keyword_recursive :blank)
                keyword_view
                (:choice _if_not_exists :blank)
                object_reference
                (:choice
                 (:seq "(" (:choice (:seq identifier (:repeat (:seq "," identifier))) :blank) ")")
                 :blank)
                keyword_as
                create_query
                (:choice
                 (:seq
                  keyword_with
                  (:choice (:choice keyword_local keyword_cascaded) :blank)
                  _check_option)
                 :blank)))
  create_materialized_view (:prec-right 0
                            (:seq
                             keyword_create
                             (:choice _or_replace :blank)
                             keyword_materialized
                             keyword_view
                             (:choice _if_not_exists :blank)
                             object_reference
                             keyword_as
                             create_query
                             (:choice
                              (:choice
                               (:seq keyword_with keyword_data)
                               (:seq keyword_with keyword_no keyword_data))
                              :blank)))
  dollar_quote (:pattern "\\$[^\\$]*\\$")
  create_function (:seq
                   keyword_create
                   (:choice _or_replace :blank)
                   keyword_function
                   object_reference
                   function_arguments
                   keyword_returns
                   (:choice
                    _type
                    (:seq keyword_setof _type)
                    (:seq keyword_table column_definitions)
                    keyword_trigger)
                   (:repeat
                    (:choice
                     function_language
                     function_volatility
                     function_leakproof
                     function_security
                     function_safety
                     function_strictness
                     function_cost
                     function_rows
                     function_support))
                   function_body
                   (:repeat
                    (:choice
                     function_language
                     function_volatility
                     function_leakproof
                     function_security
                     function_safety
                     function_strictness
                     function_cost
                     function_rows
                     function_support)))
  _function_return (:seq keyword_return _expression)
  function_declaration (:seq
                        identifier
                        _type
                        (:choice (:seq ":=" (:choice (:seq "(" statement ")") literal)) :blank)
                        ";")
  _function_body_statement (:choice statement _function_return)
  function_body (:choice
                 (:seq _function_return ";")
                 (:seq
                  keyword_begin
                  keyword_atomic
                  (:repeat1 (:seq _function_body_statement ";"))
                  keyword_end)
                 (:seq
                  keyword_as
                  (:alias _dollar_quoted_string_start_tag dollar_quote)
                  (:choice (:seq keyword_declare (:repeat1 function_declaration)) :blank)
                  keyword_begin
                  (:repeat1 (:seq _function_body_statement ";"))
                  keyword_end
                  (:choice ";" :blank)
                  (:alias _dollar_quoted_string_end_tag dollar_quote))
                 (:seq
                  keyword_as
                  (:alias (:choice _single_quote_string _double_quote_string) literal))
                 (:seq
                  keyword_as
                  (:alias _dollar_quoted_string_start_tag dollar_quote)
                  _function_body_statement
                  (:choice ";" :blank)
                  (:alias _dollar_quoted_string_end_tag dollar_quote)))
  function_language (:seq keyword_language identifier)
  function_volatility (:choice keyword_immutable keyword_stable keyword_volatile)
  function_leakproof (:seq (:choice keyword_not :blank) keyword_leakproof)
  function_security (:seq
                     (:choice keyword_external :blank)
                     keyword_security
                     (:choice keyword_invoker keyword_definer))
  function_safety (:seq keyword_parallel (:choice keyword_safe keyword_unsafe keyword_restricted))
  function_strictness (:choice
                       (:seq
                        (:choice keyword_called (:seq keyword_returns keyword_null))
                        keyword_on
                        keyword_null
                        keyword_input)
                       keyword_strict)
  function_cost (:seq keyword_cost _natural_number)
  function_rows (:seq keyword_rows _natural_number)
  function_support (:seq keyword_support (:alias _literal_string literal))
  _operator_class (:seq
                   (:field :opclass identifier)
                   (:choice
                    (:field :opclass_parameters
                     (:seq "(" (:choice (:seq term (:repeat (:seq "," term))) :blank) ")"))
                    :blank))
  _index_field (:seq
                (:choice
                 (:field :expression (:seq "(" _expression ")"))
                 (:field :function invocation)
                 (:field :column _column))
                (:choice (:seq keyword_collate identifier) :blank)
                (:choice _operator_class :blank)
                (:choice direction :blank)
                (:choice (:seq keyword_nulls (:choice keyword_first keyword_last)) :blank))
  index_fields (:seq
                "("
                (:choice
                 (:seq (:alias _index_field field) (:repeat (:seq "," (:alias _index_field field))))
                 :blank)
                ")")
  create_index (:seq
                keyword_create
                (:choice keyword_unique :blank)
                keyword_index
                (:choice keyword_concurrently :blank)
                (:choice (:seq (:choice _if_not_exists :blank) (:field :column _column)) :blank)
                keyword_on
                (:choice keyword_only :blank)
                (:seq
                 object_reference
                 (:choice
                  (:seq
                   keyword_using
                   (:choice
                    keyword_btree
                    keyword_hash
                    keyword_gist
                    keyword_spgist
                    keyword_gin
                    keyword_brin))
                  :blank)
                 index_fields)
                (:choice where :blank))
  create_schema (:prec-left 0
                 (:seq
                  keyword_create
                  keyword_schema
                  (:choice
                   (:seq
                    (:choice _if_not_exists :blank)
                    identifier
                    (:choice (:seq keyword_authorization identifier) :blank))
                   (:seq keyword_authorization identifier))))
  _with_settings (:seq
                  (:field :name identifier)
                  (:choice "=" :blank)
                  (:field :value (:choice identifier (:alias _single_quote_string literal))))
  create_database (:seq
                   keyword_create
                   keyword_database
                   (:choice _if_not_exists :blank)
                   identifier
                   (:choice keyword_with :blank)
                   (:repeat _with_settings))
  create_role (:seq
               keyword_create
               (:choice keyword_user keyword_role keyword_group)
               identifier
               (:choice keyword_with :blank)
               (:repeat (:choice _user_access_role_config _role_options)))
  _role_options (:choice
                 (:field :option identifier)
                 (:seq
                  keyword_valid
                  keyword_until
                  (:field :valid_until (:alias _literal_string literal)))
                 (:seq
                  keyword_connection
                  keyword_limit
                  (:field :connection_limit (:alias _integer literal)))
                 (:seq
                  (:choice keyword_encrypted :blank)
                  keyword_password
                  (:choice (:field :password (:alias _literal_string literal)) keyword_null)))
  _user_access_role_config (:seq
                            (:choice
                             (:seq (:choice keyword_in :blank) keyword_role)
                             (:seq keyword_in keyword_group)
                             keyword_admin
                             keyword_user)
                            (:seq identifier (:repeat (:seq "," identifier))))
  create_sequence (:seq
                   keyword_create
                   (:choice
                    (:choice (:choice keyword_temporary keyword_temp) keyword_unlogged)
                    :blank)
                   keyword_sequence
                   (:choice _if_not_exists :blank)
                   object_reference
                   (:repeat
                    (:choice
                     (:seq keyword_as _type)
                     (:seq
                      keyword_increment
                      (:choice keyword_by :blank)
                      (:field :increment (:alias _integer literal)))
                     (:seq keyword_minvalue (:choice literal (:seq keyword_no keyword_minvalue)))
                     (:seq keyword_no keyword_minvalue)
                     (:seq keyword_maxvalue (:choice literal (:seq keyword_no keyword_maxvalue)))
                     (:seq keyword_no keyword_maxvalue)
                     (:seq
                      keyword_start
                      (:choice keyword_with :blank)
                      (:field :start (:alias _integer literal)))
                     (:seq keyword_cache (:field :cache (:alias _integer literal)))
                     (:seq (:choice keyword_no :blank) keyword_cycle)
                     (:seq keyword_owned keyword_by (:choice keyword_none object_reference)))))
  create_extension (:seq
                    keyword_create
                    keyword_extension
                    (:choice _if_not_exists :blank)
                    identifier
                    (:choice keyword_with :blank)
                    (:choice (:seq keyword_schema identifier) :blank)
                    (:choice
                     (:seq keyword_version (:choice identifier (:alias _literal_string literal)))
                     :blank)
                    (:choice keyword_cascade :blank))
  create_trigger (:seq
                  keyword_create
                  (:choice _or_replace :blank)
                  (:choice (:seq keyword_definer "=" identifier) :blank)
                  (:choice keyword_constraint :blank)
                  (:choice _temporary :blank)
                  keyword_trigger
                  (:choice _if_not_exists :blank)
                  object_reference
                  (:choice keyword_before keyword_after (:seq keyword_instead keyword_of))
                  _create_trigger_event
                  (:repeat (:seq keyword_or _create_trigger_event))
                  keyword_on
                  object_reference
                  (:repeat
                   (:choice
                    (:seq keyword_from object_reference)
                    (:choice
                     (:seq keyword_not keyword_deferrable)
                     keyword_deferrable
                     (:seq keyword_initially keyword_immediate)
                     (:seq keyword_initially keyword_deferred))
                    (:seq
                     keyword_referencing
                     (:choice keyword_old keyword_new)
                     keyword_table
                     (:choice keyword_as :blank)
                     identifier)
                    (:seq
                     keyword_for
                     (:choice keyword_each :blank)
                     (:choice keyword_row keyword_statement)
                     (:choice (:seq (:choice keyword_follows keyword_precedes) identifier) :blank))
                    (:seq keyword_when (:seq "(" _expression ")"))))
                  keyword_execute
                  (:choice keyword_function keyword_procedure)
                  object_reference
                  (:seq
                   "("
                   (:choice
                    (:seq (:field :parameter term) (:repeat (:seq "," (:field :parameter term))))
                    :blank)
                   ")"))
  _create_trigger_event (:choice
                         keyword_insert
                         (:seq
                          keyword_update
                          (:choice
                           (:seq keyword_of (:seq identifier (:repeat (:seq "," identifier))))
                           :blank))
                         keyword_delete
                         keyword_truncate)
  create_type (:seq
               keyword_create
               keyword_type
               object_reference
               (:choice
                (:seq
                 (:choice
                  (:seq
                   keyword_as
                   column_definitions
                   (:choice (:seq keyword_collate identifier) :blank))
                  (:seq keyword_as keyword_enum enum_elements)
                  (:seq
                   (:choice (:seq keyword_as keyword_range) :blank)
                   (:seq
                    "("
                    (:choice (:seq _with_settings (:repeat (:seq "," _with_settings))) :blank)
                    ")"))))
                :blank))
  enum_elements (:seq
                 (:seq
                  "("
                  (:choice
                   (:seq
                    (:field :enum_element (:alias _literal_string literal))
                    (:repeat (:seq "," (:field :enum_element (:alias _literal_string literal)))))
                   :blank)
                  ")"))
  _alter_statement (:seq
                    (:choice
                     alter_table
                     alter_view
                     alter_schema
                     alter_type
                     alter_index
                     alter_database
                     alter_role
                     alter_sequence))
  _rename_statement (:seq
                     keyword_rename
                     (:choice keyword_table keyword_tables)
                     (:choice _if_exists :blank)
                     object_reference
                     (:choice
                      (:choice
                       keyword_nowait
                       (:seq keyword_wait (:field :timeout (:alias _natural_number literal))))
                      :blank)
                     keyword_to
                     object_reference
                     (:repeat (:seq "," _rename_table_names)))
  _rename_table_names (:seq object_reference keyword_to object_reference)
  alter_table (:seq
               keyword_alter
               keyword_table
               (:choice _if_exists :blank)
               (:choice keyword_only :blank)
               object_reference
               (:choice (:seq _alter_specifications (:repeat (:seq "," _alter_specifications)))))
  _alter_specifications (:choice
                         add_column
                         add_constraint
                         drop_constraint
                         alter_column
                         modify_column
                         change_column
                         drop_column
                         rename_object
                         rename_column
                         set_schema
                         change_ownership)
  add_column (:seq
              (:choice keyword_add :blank)
              (:choice keyword_column :blank)
              (:choice _if_not_exists :blank)
              column_definition
              (:choice column_position :blank))
  add_constraint (:seq keyword_add (:choice keyword_constraint :blank) identifier constraint)
  drop_constraint (:seq
                   keyword_drop
                   keyword_constraint
                   (:choice _if_exists :blank)
                   identifier
                   (:choice _drop_behavior :blank))
  alter_column (:seq
                keyword_alter
                (:choice keyword_column :blank)
                (:field :name identifier)
                (:choice
                 (:seq (:choice keyword_set keyword_drop) keyword_not keyword_null)
                 (:seq
                  (:choice (:seq keyword_set keyword_data) :blank)
                  keyword_type
                  (:field :type _type))
                 (:seq
                  keyword_set
                  (:choice
                   (:seq keyword_statistics (:field :statistics _integer))
                   (:seq
                    keyword_storage
                    (:choice
                     keyword_plain
                     keyword_external
                     keyword_extended
                     keyword_main
                     keyword_default))
                   (:seq keyword_compression (:field :compression_method _identifier))
                   (:seq (:seq "(" (:seq _key_value_pair (:repeat (:seq "," _key_value_pair))) ")"))
                   (:seq keyword_default _expression)))
                 (:seq keyword_drop keyword_default)))
  modify_column (:seq
                 keyword_modify
                 (:choice keyword_column :blank)
                 (:choice _if_exists :blank)
                 column_definition
                 (:choice column_position :blank))
  change_column (:seq
                 keyword_change
                 (:choice keyword_column :blank)
                 (:choice _if_exists :blank)
                 (:field :old_name identifier)
                 column_definition
                 (:choice column_position :blank))
  column_position (:choice keyword_first (:seq keyword_after (:field :col_name identifier)))
  drop_column (:seq
               keyword_drop
               (:choice keyword_column :blank)
               (:choice _if_exists :blank)
               (:field :name identifier))
  rename_column (:seq
                 keyword_rename
                 (:choice keyword_column :blank)
                 (:field :old_name identifier)
                 keyword_to
                 (:field :new_name identifier))
  alter_view (:seq
              keyword_alter
              keyword_view
              (:choice _if_exists :blank)
              object_reference
              (:choice rename_object rename_column set_schema change_ownership))
  alter_schema (:seq
                keyword_alter
                keyword_schema
                identifier
                (:choice keyword_rename keyword_owner)
                keyword_to
                identifier)
  alter_database (:seq
                  keyword_alter
                  keyword_database
                  identifier
                  (:choice keyword_with :blank)
                  (:choice
                   (:seq rename_object)
                   (:seq change_ownership)
                   (:seq
                    keyword_reset
                    (:choice keyword_all (:field :configuration_parameter identifier)))
                   (:seq
                    keyword_set
                    (:choice (:seq keyword_tablespace identifier) set_configuration))))
  alter_role (:seq
              keyword_alter
              (:choice keyword_role keyword_group keyword_user)
              (:choice identifier keyword_all)
              (:choice
               rename_object
               (:seq (:choice keyword_with :blank) (:repeat _role_options))
               (:seq
                (:choice (:seq keyword_in keyword_database identifier) :blank)
                (:choice
                 (:seq keyword_set set_configuration)
                 (:seq keyword_reset (:choice keyword_all (:field :option identifier)))))))
  set_configuration (:seq
                     (:field :option identifier)
                     (:choice
                      (:seq keyword_from keyword_current)
                      (:seq
                       (:choice keyword_to "=")
                       (:choice (:field :parameter identifier) literal keyword_default))))
  alter_index (:seq
               keyword_alter
               keyword_index
               (:choice _if_exists :blank)
               identifier
               (:choice
                rename_object
                (:seq
                 keyword_alter
                 (:choice keyword_column :blank)
                 (:alias _natural_number literal)
                 keyword_set
                 keyword_statistics
                 (:alias _natural_number literal))
                (:seq
                 keyword_reset
                 (:seq "(" (:choice (:seq identifier (:repeat (:seq "," identifier))) :blank) ")"))
                (:seq
                 keyword_set
                 (:choice
                  (:seq keyword_tablespace identifier)
                  (:seq
                   "("
                   (:choice
                    (:seq
                     (:seq identifier "=" (:field :value literal))
                     (:repeat (:seq "," (:seq identifier "=" (:field :value literal)))))
                    :blank)
                   ")")))))
  alter_sequence (:seq
                  keyword_alter
                  keyword_sequence
                  (:choice _if_exists :blank)
                  object_reference
                  (:choice
                   (:repeat1
                    (:choice
                     (:seq keyword_as _type)
                     (:seq keyword_increment (:choice keyword_by :blank) literal)
                     (:seq keyword_minvalue (:choice literal (:seq keyword_no keyword_minvalue)))
                     (:seq keyword_maxvalue (:choice literal (:seq keyword_no keyword_maxvalue)))
                     (:seq
                      keyword_start
                      (:choice keyword_with :blank)
                      (:field :start (:alias _integer literal)))
                     (:seq
                      keyword_restart
                      (:choice keyword_with :blank)
                      (:field :restart (:alias _integer literal)))
                     (:seq keyword_cache (:field :cache (:alias _integer literal)))
                     (:seq (:choice keyword_no :blank) keyword_cycle)
                     (:seq keyword_owned keyword_by (:choice keyword_none object_reference))))
                   rename_object
                   change_ownership
                   (:seq
                    keyword_set
                    (:choice
                     (:choice keyword_logged keyword_unlogged)
                     (:seq keyword_schema identifier)))))
  alter_type (:seq
              keyword_alter
              keyword_type
              identifier
              (:choice
               change_ownership
               set_schema
               rename_object
               (:seq
                keyword_rename
                keyword_attribute
                identifier
                keyword_to
                identifier
                (:choice _drop_behavior :blank))
               (:seq
                keyword_add
                keyword_value
                (:choice _if_not_exists :blank)
                (:alias _single_quote_string literal)
                (:choice
                 (:seq (:choice keyword_before keyword_after) (:alias _single_quote_string literal))
                 :blank))
               (:seq
                keyword_rename
                keyword_value
                (:alias _single_quote_string literal)
                keyword_to
                (:alias _single_quote_string literal))
               (:seq
                (:choice
                 (:seq keyword_add keyword_attribute identifier _type)
                 (:seq keyword_drop keyword_attribute (:choice _if_exists :blank) identifier)
                 (:seq
                  keyword_alter
                  keyword_attribute
                  identifier
                  (:choice (:seq keyword_set keyword_data) :blank)
                  keyword_type
                  _type))
                (:choice (:seq keyword_collate identifier) :blank)
                (:choice _drop_behavior :blank))))
  _drop_behavior (:choice keyword_cascade keyword_restrict)
  _drop_statement (:seq
                   (:choice
                    drop_table
                    drop_view
                    drop_index
                    drop_type
                    drop_schema
                    drop_database
                    drop_role
                    drop_sequence
                    drop_extension
                    drop_function))
  drop_table (:seq
              keyword_drop
              keyword_table
              (:choice _if_exists :blank)
              object_reference
              (:choice _drop_behavior :blank))
  drop_view (:seq
             keyword_drop
             keyword_view
             (:choice _if_exists :blank)
             object_reference
             (:choice _drop_behavior :blank))
  drop_schema (:seq
               keyword_drop
               keyword_schema
               (:choice _if_exists :blank)
               identifier
               (:choice _drop_behavior :blank))
  drop_database (:seq
                 keyword_drop
                 keyword_database
                 (:choice _if_exists :blank)
                 identifier
                 (:choice keyword_with :blank)
                 (:choice keyword_force :blank))
  drop_role (:seq
             keyword_drop
             (:choice keyword_group keyword_role keyword_user)
             (:choice _if_exists :blank)
             identifier)
  drop_type (:seq
             keyword_drop
             keyword_type
             (:choice _if_exists :blank)
             object_reference
             (:choice _drop_behavior :blank))
  drop_sequence (:seq
                 keyword_drop
                 keyword_sequence
                 (:choice _if_exists :blank)
                 object_reference
                 (:choice _drop_behavior :blank))
  drop_index (:seq
              keyword_drop
              keyword_index
              (:choice keyword_concurrently :blank)
              (:choice _if_exists :blank)
              (:field :name identifier)
              (:choice _drop_behavior :blank)
              (:choice (:seq keyword_on object_reference) :blank))
  drop_extension (:seq
                  keyword_drop
                  keyword_extension
                  (:choice _if_exists :blank)
                  (:seq identifier (:repeat (:seq "," identifier)))
                  (:choice (:choice keyword_cascade keyword_restrict) :blank))
  drop_function (:seq
                 keyword_drop
                 keyword_function
                 (:choice _if_exists :blank)
                 object_reference
                 (:choice _drop_behavior :blank))
  rename_object (:seq keyword_rename keyword_to object_reference)
  set_schema (:seq keyword_set keyword_schema (:field :schema identifier))
  change_ownership (:seq keyword_owner keyword_to identifier)
  object_id (:seq
             keyword_object_id
             (:seq
              "("
              (:seq
               (:alias _literal_string literal)
               (:choice (:seq "," (:alias _literal_string literal)) :blank))
              ")"))
  object_reference (:choice
                    (:seq
                     (:field :database identifier)
                     "."
                     (:field :schema identifier)
                     "."
                     (:field :name identifier))
                    (:seq (:field :schema identifier) "." (:field :name identifier))
                    (:field :name identifier))
  _copy_statement (:seq
                   keyword_copy
                   object_reference
                   _column_list
                   keyword_from
                   (:choice
                    keyword_stdin
                    (:alias _literal_string "filename")
                    (:seq keyword_program (:alias _literal_string "command")))
                   (:choice keyword_with :blank)
                   (:seq
                    "("
                    (:repeat1
                     (:choice
                      (:seq keyword_format (:choice keyword_csv keyword_binary keyword_text))
                      (:seq keyword_freeze (:choice keyword_true keyword_false))
                      (:seq keyword_header (:choice keyword_true keyword_false keyword_match))
                      (:seq
                       (:choice
                        keyword_delimiter
                        keyword_null
                        keyword_default
                        keyword_escape
                        keyword_quote
                        keyword_encoding)
                       (:alias _literal_string identifier))
                      (:seq
                       (:choice keyword_force_null keyword_force_not_null keyword_force_quote)
                       _column_list)))
                    ")")
                   (:choice where :blank))
  _insert_statement (:seq insert (:choice returning :blank))
  insert (:seq
          (:choice keyword_insert keyword_replace)
          (:choice (:choice keyword_low_priority keyword_delayed keyword_high_priority) :blank)
          (:choice keyword_ignore :blank)
          (:choice (:choice keyword_into keyword_overwrite) :blank)
          object_reference
          (:choice table_partition :blank)
          (:choice (:seq keyword_as (:field :alias identifier)) :blank)
          (:choice _insert_values _set_values)
          (:choice (:choice _on_conflict _on_duplicate_key_update) :blank))
  _on_conflict (:seq
                keyword_on
                keyword_conflict
                (:seq
                 keyword_do
                 (:choice keyword_nothing (:seq keyword_update _set_values (:choice where :blank)))))
  _on_duplicate_key_update (:seq
                            keyword_on
                            keyword_duplicate
                            keyword_key
                            keyword_update
                            assignment_list)
  assignment_list (:seq assignment (:repeat (:seq "," assignment)))
  _insert_values (:seq
                  (:choice (:alias _column_list list) :blank)
                  (:choice (:seq keyword_values (:seq list (:repeat (:seq "," list)))) _dml_read))
  _set_values (:seq keyword_set (:seq assignment (:repeat (:seq "," assignment))))
  _column_list (:seq
                "("
                (:seq (:alias _column column) (:repeat (:seq "," (:alias _column column))))
                ")")
  _column (:choice identifier (:alias _literal_string literal))
  _update_statement (:seq update (:choice returning :blank))
  _merge_statement (:seq
                    keyword_merge
                    keyword_into
                    object_reference
                    (:choice _alias :blank)
                    keyword_using
                    (:choice subquery object_reference)
                    (:choice _alias :blank)
                    keyword_on
                    (:prec-right 0
                     (:choice
                      (:field :predicate _expression)
                      (:seq "(" (:field :predicate _expression) ")")))
                    (:repeat1 when_clause))
  when_clause (:seq
               keyword_when
               (:choice keyword_not :blank)
               keyword_matched
               (:choice
                (:seq
                 keyword_and
                 (:prec-right 0
                  (:choice
                   (:field :predicate _expression)
                   (:seq "(" (:field :predicate _expression) ")"))))
                :blank)
               keyword_then
               (:choice
                keyword_delete
                (:seq keyword_update _set_values)
                (:seq keyword_insert _insert_values)
                (:choice where :blank)))
  _optimize_statement (:choice _compute_stats _vacuum_table _optimize_table)
  _compute_stats (:choice
                  (:seq
                   keyword_analyze
                   keyword_table
                   object_reference
                   (:choice _partition_spec :blank)
                   keyword_compute
                   keyword_statistics
                   (:choice (:seq keyword_for keyword_columns) :blank)
                   (:choice (:seq keyword_cache keyword_metadata) :blank)
                   (:choice keyword_noscan :blank))
                  (:seq
                   keyword_compute
                   (:choice keyword_incremental :blank)
                   keyword_stats
                   object_reference
                   (:choice
                    (:choice
                     (:seq
                      "("
                      (:choice (:seq (:repeat1 field) (:repeat (:seq "," (:repeat1 field)))) :blank)
                      ")")
                     _partition_spec)
                    :blank)))
  _optimize_table (:choice
                   (:seq
                    keyword_optimize
                    object_reference
                    keyword_rewrite
                    keyword_data
                    keyword_using
                    keyword_bin_pack
                    (:choice where :blank))
                   (:seq
                    keyword_optimize
                    (:choice (:choice keyword_local) :blank)
                    keyword_table
                    object_reference
                    (:repeat (:seq "," object_reference))))
  _vacuum_table (:seq
                 keyword_vacuum
                 (:choice _vacuum_option :blank)
                 object_reference
                 (:choice
                  (:seq "(" (:choice (:seq field (:repeat (:seq "," field))) :blank) ")")
                  :blank))
  _vacuum_option (:choice
                  (:seq keyword_full (:choice (:choice keyword_true keyword_false) :blank))
                  (:seq keyword_parallel (:choice (:choice keyword_true keyword_false) :blank))
                  (:seq keyword_analyze (:choice (:choice keyword_true keyword_false) :blank)))
  _partition_spec (:seq
                   keyword_partition
                   (:seq "(" (:seq table_option (:repeat (:seq "," table_option))) ")"))
  update (:seq
          keyword_update
          (:choice keyword_only :blank)
          (:choice _mysql_update_statement _postgres_update_statement))
  _mysql_update_statement (:prec 0
                           (:seq
                            (:seq relation (:repeat (:seq "," relation)))
                            (:repeat join)
                            _set_values
                            (:choice where :blank)))
  _postgres_update_statement (:prec 1 (:seq relation _set_values (:choice from :blank)))
  storage_location (:prec-right 0
                    (:seq
                     keyword_location
                     (:field :path (:alias _literal_string literal))
                     (:choice
                      (:seq
                       keyword_cached
                       keyword_in
                       (:field :pool (:alias _literal_string literal))
                       (:choice
                        (:choice
                         keyword_uncached
                         (:seq
                          keyword_with
                          keyword_replication
                          "="
                          (:field :value (:alias _natural_number literal))))
                        :blank))
                      :blank)))
  row_format (:seq
              keyword_row
              keyword_format
              keyword_delimited
              (:choice
               (:seq
                keyword_fields
                keyword_terminated
                keyword_by
                (:field :fields_terminated_char (:alias _literal_string literal))
                (:choice
                 (:seq
                  keyword_escaped
                  keyword_by
                  (:field :escaped_char (:alias _literal_string literal)))
                 :blank))
               :blank)
              (:choice
               (:seq
                keyword_lines
                keyword_terminated
                keyword_by
                (:field :row_terminated_char (:alias _literal_string literal)))
               :blank))
  table_sort (:seq
              keyword_sort
              keyword_by
              (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")"))
  table_partition (:seq
                   (:choice
                    (:seq keyword_partition keyword_by (:choice keyword_range keyword_hash))
                    (:seq keyword_partitioned keyword_by)
                    keyword_partition)
                   (:choice
                    (:seq
                     "("
                     (:choice (:seq identifier (:repeat (:seq "," identifier))) :blank)
                     ")")
                    column_definitions
                    (:seq "(" (:seq _key_value_pair (:repeat (:seq "," _key_value_pair))) ")")))
  _key_value_pair (:seq
                   (:field :key identifier)
                   "="
                   (:field :value (:alias _literal_string literal)))
  stored_as (:seq
             keyword_stored
             keyword_as
             (:choice
              keyword_parquet
              keyword_csv
              keyword_sequencefile
              keyword_textfile
              keyword_rcfile
              keyword_orc
              keyword_avro
              keyword_jsonfile))
  assignment (:seq (:field :left (:alias _qualified_field field)) "=" (:field :right _expression))
  table_option (:choice
                (:seq keyword_default keyword_character keyword_set identifier)
                (:seq keyword_collate identifier)
                (:field :name keyword_default)
                (:seq
                 (:field :name (:choice keyword_engine identifier _literal_string))
                 "="
                 (:field :value (:choice identifier _literal_string))))
  column_definitions (:seq
                      "("
                      (:seq column_definition (:repeat (:seq "," column_definition)))
                      (:choice constraints :blank)
                      ")")
  column_definition (:seq (:field :name _column) (:field :type _type) (:repeat _column_constraint))
  _column_comment (:seq keyword_comment (:alias _literal_string literal))
  _column_constraint (:prec-left 0
                      (:choice
                       (:choice keyword_null _not_null)
                       (:seq
                        keyword_references
                        object_reference
                        (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")")
                        (:repeat
                         (:seq
                          keyword_on
                          (:choice keyword_delete keyword_update)
                          (:choice
                           (:seq keyword_no keyword_action)
                           keyword_restrict
                           keyword_cascade
                           (:seq
                            keyword_set
                            (:choice keyword_null keyword_default)
                            (:choice
                             (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")")
                             :blank))))))
                       _default_expression
                       _primary_key
                       keyword_auto_increment
                       direction
                       _column_comment
                       _check_constraint
                       (:seq
                        (:choice (:seq keyword_generated keyword_always) :blank)
                        keyword_as
                        _expression)
                       (:choice keyword_stored keyword_virtual)
                       keyword_unique))
  _check_constraint (:seq
                     (:choice (:seq keyword_constraint literal) :blank)
                     keyword_check
                     (:seq "(" binary_expression ")"))
  _default_expression (:seq
                       keyword_default
                       (:prec-right 0
                        (:choice _inner_default_expression (:seq "(" _inner_default_expression ")"))))
  _inner_default_expression (:choice
                             literal
                             list
                             cast
                             binary_expression
                             unary_expression
                             array
                             invocation
                             keyword_current_timestamp
                             (:alias implicit_cast cast))
  constraints (:seq "," constraint (:repeat (:seq "," constraint)))
  constraint (:choice _constraint_literal _key_constraint _primary_key_constraint _check_constraint)
  _constraint_literal (:seq
                       keyword_constraint
                       (:field :name identifier)
                       (:choice (:seq _primary_key ordered_columns) (:seq _check_constraint)))
  _primary_key_constraint (:seq _primary_key ordered_columns)
  _key_constraint (:seq
                   (:choice
                    (:seq
                     keyword_unique
                     (:choice
                      (:choice
                       keyword_index
                       keyword_key
                       (:seq keyword_nulls (:choice keyword_not :blank) keyword_distinct))
                      :blank))
                    (:seq
                     (:choice keyword_foreign :blank)
                     keyword_key
                     (:choice _if_not_exists :blank))
                    keyword_index)
                   (:choice (:field :name identifier) :blank)
                   ordered_columns
                   (:choice
                    (:seq
                     keyword_references
                     object_reference
                     (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")")
                     (:repeat
                      (:seq
                       keyword_on
                       (:choice keyword_delete keyword_update)
                       (:choice
                        (:seq keyword_no keyword_action)
                        keyword_restrict
                        keyword_cascade
                        (:seq
                         keyword_set
                         (:choice keyword_null keyword_default)
                         (:choice
                          (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")")
                          :blank))))))
                    :blank))
  ordered_columns (:seq
                   "("
                   (:seq
                    (:alias ordered_column column)
                    (:repeat (:seq "," (:alias ordered_column column))))
                   ")")
  ordered_column (:seq (:field :name _column) (:choice direction :blank))
  all_fields (:seq (:choice (:seq object_reference ".") :blank) "*")
  parameter (:pattern "\\?|(\\$[0-9]+)")
  case (:seq
        keyword_case
        (:choice
         (:seq
          _expression
          keyword_when
          _expression
          keyword_then
          _expression
          (:repeat (:seq keyword_when _expression keyword_then _expression)))
         (:seq
          keyword_when
          _expression
          keyword_then
          _expression
          (:repeat (:seq keyword_when _expression keyword_then _expression))))
        (:choice (:seq keyword_else _expression) :blank)
        keyword_end)
  field (:field :name identifier)
  _qualified_field (:seq
                    (:choice
                     (:seq
                      (:prec-right 0 (:choice object_reference (:seq "(" object_reference ")")))
                      ".")
                     :blank)
                    (:field :name identifier))
  implicit_cast (:seq _expression "::" _type)
  interval (:seq keyword_interval _literal_string)
  cast (:seq
        (:field :name keyword_cast)
        (:seq "(" (:seq (:field :parameter _expression) keyword_as _type) ")"))
  filter_expression (:seq keyword_filter (:seq "(" where ")"))
  invocation (:prec 1
              (:seq
               object_reference
               (:choice
                (:seq
                 "("
                 (:choice
                  (:seq
                   (:seq
                    (:choice keyword_distinct :blank)
                    (:field :parameter term)
                    (:choice order_by :blank))
                   (:repeat
                    (:seq
                     ","
                     (:seq
                      (:choice keyword_distinct :blank)
                      (:field :parameter term)
                      (:choice order_by :blank)))))
                  :blank)
                 ")")
                (:seq
                 "("
                 (:choice
                  (:seq
                   (:seq (:field :unit object_reference) keyword_from term)
                   (:repeat (:seq "," (:seq (:field :unit object_reference) keyword_from term))))
                  :blank)
                 ")")
                (:seq
                 "("
                 (:seq
                  (:choice keyword_distinct :blank)
                  (:field :parameter term)
                  (:choice order_by :blank)
                  (:choice
                   (:seq (:choice keyword_separator ",") (:alias _literal_string literal))
                   :blank)
                  (:choice limit :blank))
                 ")"))
               (:choice filter_expression :blank)))
  exists (:seq keyword_exists subquery)
  partition_by (:seq
                keyword_partition
                keyword_by
                (:seq _expression (:repeat (:seq "," _expression))))
  frame_definition (:seq
                    (:choice
                     (:seq keyword_unbounded keyword_preceding)
                     (:seq
                      (:field :start
                       (:choice
                        identifier
                        binary_expression
                        (:alias _literal_string literal)
                        (:alias _integer literal)))
                      keyword_preceding)
                     _current_row
                     (:seq
                      (:field :end
                       (:choice
                        identifier
                        binary_expression
                        (:alias _literal_string literal)
                        (:alias _integer literal)))
                      keyword_following)
                     (:seq keyword_unbounded keyword_following)))
  window_frame (:seq
                (:choice keyword_range keyword_rows keyword_groups)
                (:choice
                 (:seq
                  keyword_between
                  frame_definition
                  (:choice (:seq keyword_and frame_definition) :blank))
                 (:seq frame_definition))
                (:choice
                 (:choice _exclude_current_row _exclude_group _exclude_ties _exclude_no_others)
                 :blank))
  window_clause (:seq keyword_window identifier keyword_as window_specification)
  window_specification (:seq
                        "("
                        (:seq
                         (:choice partition_by :blank)
                         (:choice order_by :blank)
                         (:choice window_frame :blank))
                        ")")
  window_function (:seq invocation keyword_over (:choice identifier window_specification))
  _alias (:seq (:choice keyword_as :blank) (:field :alias identifier))
  from (:seq
        keyword_from
        (:choice keyword_only :blank)
        (:seq relation (:repeat (:seq "," relation)))
        (:choice index_hint :blank)
        (:repeat (:choice join cross_join lateral_join lateral_cross_join))
        (:choice where :blank)
        (:choice group_by :blank)
        (:choice window_clause :blank)
        (:choice order_by :blank)
        (:choice limit :blank))
  relation (:prec-right 0
            (:seq
             (:choice subquery invocation object_reference (:seq "(" values ")"))
             (:choice (:seq _alias (:choice (:alias _column_list list) :blank)) :blank)))
  values (:seq keyword_values list (:choice (:repeat (:seq "," list)) :blank))
  index_hint (:seq
              (:choice keyword_force keyword_use keyword_ignore)
              keyword_index
              (:choice (:seq keyword_for keyword_join) :blank)
              (:seq "(" (:field :index_name identifier) ")"))
  join (:seq
        (:choice keyword_natural :blank)
        (:choice
         (:choice
          keyword_left
          (:seq keyword_full keyword_outer)
          (:seq keyword_left keyword_outer)
          keyword_right
          (:seq keyword_right keyword_outer)
          keyword_inner
          keyword_full)
         :blank)
        keyword_join
        relation
        (:choice index_hint :blank)
        (:choice join :blank)
        (:choice
         (:seq keyword_on (:field :predicate _expression))
         (:seq keyword_using (:alias _column_list list))))
  cross_join (:prec-right 0
              (:seq
               keyword_cross
               keyword_join
               relation
               (:choice
                (:seq
                 keyword_with
                 keyword_ordinality
                 (:choice
                  (:seq
                   keyword_as
                   (:field :alias identifier)
                   (:seq "(" (:choice (:seq identifier (:repeat (:seq "," identifier))) :blank) ")"))
                  :blank))
                :blank)))
  lateral_join (:seq
                (:choice
                 (:choice keyword_left (:seq keyword_left keyword_outer) keyword_inner)
                 :blank)
                keyword_join
                keyword_lateral
                (:choice invocation subquery)
                (:choice
                 (:choice (:seq keyword_as (:field :alias identifier)) (:field :alias identifier))
                 :blank)
                keyword_on
                (:choice _expression keyword_true keyword_false))
  lateral_cross_join (:seq
                      keyword_cross
                      keyword_join
                      keyword_lateral
                      (:choice invocation subquery)
                      (:choice
                       (:choice
                        (:seq keyword_as (:field :alias identifier))
                        (:field :alias identifier))
                       :blank))
  where (:seq keyword_where (:field :predicate _expression))
  group_by (:seq
            keyword_group
            keyword_by
            (:seq _expression (:repeat (:seq "," _expression)))
            (:choice _having :blank))
  _having (:seq keyword_having _expression)
  order_by (:prec-right 0
            (:seq keyword_order keyword_by (:seq order_target (:repeat (:seq "," order_target)))))
  order_target (:seq
                _expression
                (:choice
                 (:seq
                  (:choice direction (:seq keyword_using (:choice "<" ">" "<=" ">=")))
                  (:choice (:seq keyword_nulls (:choice keyword_first keyword_last)) :blank))
                 :blank))
  limit (:seq keyword_limit literal (:choice offset :blank))
  offset (:seq keyword_offset literal)
  returning (:seq keyword_returning select_expression)
  _expression (:prec 1
               (:choice
                literal
                (:alias _qualified_field field)
                parameter
                list
                case
                window_function
                subquery
                cast
                (:alias implicit_cast cast)
                exists
                invocation
                binary_expression
                subscript
                unary_expression
                array
                interval
                between_expression
                parenthesized_expression
                object_id))
  parenthesized_expression (:prec 2 (:seq "(" _expression ")"))
  subscript (:prec-left "binary_is"
             (:seq
              (:field :expression _expression)
              "["
              (:choice
               (:field :subscript _expression)
               (:seq (:field :lower _expression) ":" (:field :upper _expression)))
              "]"))
  op_other (:token
            (:choice
             "->"
             "->>"
             "#>"
             "#>>"
             "~"
             "!~"
             "~*"
             "!~*"
             "|"
             "&"
             "#"
             "<<"
             ">>"
             "<<="
             ">>="
             "##"
             "<->"
             "@>"
             "<@"
             "&<"
             "&>"
             "|>>"
             "<<|"
             "&<|"
             "|&>"
             "<^"
             "^>"
             "?#"
             "?-"
             "?|"
             "?-|"
             "?||"
             "@@"
             "@@@"
             "@?"
             "#-"
             "?&"
             "?"
             "-|-"
             "||"
             "^@"))
  binary_expression (:choice
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "+")
                       (:field :right _expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "-")
                       (:field :right _expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "*")
                       (:field :right _expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "/")
                       (:field :right _expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "%")
                       (:field :right _expression)))
                     (:prec-left "binary_exp"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "^")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "=")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<=")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "!=")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator ">=")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator ">")
                       (:field :right _expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<>")
                       (:field :right _expression)))
                     (:prec-left "binary_other"
                      (:seq
                       (:field :left _expression)
                       (:field :operator op_other)
                       (:field :right _expression)))
                     (:prec-left "binary_is"
                      (:seq
                       (:field :left _expression)
                       (:field :operator keyword_is)
                       (:field :right _expression)))
                     (:prec-left "binary_is"
                      (:seq
                       (:field :left _expression)
                       (:field :operator is_not)
                       (:field :right _expression)))
                     (:prec-left "pattern_matching"
                      (:seq
                       (:field :left _expression)
                       (:field :operator keyword_like)
                       (:field :right _expression)))
                     (:prec-left "pattern_matching"
                      (:seq
                       (:field :left _expression)
                       (:field :operator not_like)
                       (:field :right _expression)))
                     (:prec-left "pattern_matching"
                      (:seq
                       (:field :left _expression)
                       (:field :operator similar_to)
                       (:field :right _expression)))
                     (:prec-left "pattern_matching"
                      (:seq
                       (:field :left _expression)
                       (:field :operator not_similar_to)
                       (:field :right _expression)))
                     (:prec-left "binary_is"
                      (:seq
                       (:field :left _expression)
                       (:field :operator distinct_from)
                       (:field :right _expression)))
                     (:prec-left "binary_is"
                      (:seq
                       (:field :left _expression)
                       (:field :operator not_distinct_from)
                       (:field :right _expression)))
                     (:prec-left "clause_connective"
                      (:seq
                       (:field :left _expression)
                       (:field :operator keyword_and)
                       (:field :right _expression)))
                     (:prec-left "clause_disjunctive"
                      (:seq
                       (:field :left _expression)
                       (:field :operator keyword_or)
                       (:field :right _expression)))
                     (:prec-left "binary_in"
                      (:seq
                       (:field :left _expression)
                       (:field :operator keyword_in)
                       (:field :right (:choice list subquery))))
                     (:prec-left "binary_in"
                      (:seq
                       (:field :left _expression)
                       (:field :operator not_in)
                       (:field :right (:choice list subquery)))))
  op_unary_other (:token (:choice "|/" "||/" "@" "~" "@-@" "@@" "#" "?-" "?|" "!!"))
  unary_expression (:choice
                    (:prec-left "unary_not"
                     (:seq (:field :operator keyword_not) (:field :operand _expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator bang) (:field :operand _expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator keyword_any) (:field :operand _expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator keyword_some) (:field :operand _expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator keyword_all) (:field :operand _expression)))
                    (:prec-left "unary_other"
                     (:seq (:field :operator op_unary_other) (:field :operand _expression))))
  between_expression (:choice
                      (:prec-left "between"
                       (:seq
                        (:field :left _expression)
                        (:field :operator keyword_between)
                        (:field :low _expression)
                        keyword_and
                        (:field :high _expression)))
                      (:prec-left "between"
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:seq keyword_not keyword_between))
                        (:field :low _expression)
                        keyword_and
                        (:field :high _expression))))
  not_in (:seq keyword_not keyword_in)
  subquery (:seq "(" _dml_read ")")
  list (:seq "(" (:choice (:seq _expression (:repeat (:seq "," _expression))) :blank) ")")
  literal (:prec 2
           (:choice
            _integer
            _decimal_number
            _literal_string
            _bit_string
            _string_casting
            keyword_true
            keyword_false
            keyword_null))
  _double_quote_string (:pattern "\"[^\"]*\"")
  _single_quote_string (:seq
                        (:pattern "([uU]&|[nN])?'([^']|'')*'")
                        (:repeat (:pattern "'([^']|'')*'")))
  _postgres_escape_string (:pattern "(e|E)'([^']|\\\\')*'")
  _literal_string (:prec 1
                   (:choice
                    _single_quote_string
                    _double_quote_string
                    _dollar_quoted_string
                    _postgres_escape_string))
  _natural_number (:pattern "\\d+")
  _integer (:seq
            (:choice (:choice "-" "+") :blank)
            (:pattern "(0[xX][0-9A-Fa-f]+(_[0-9A-Fa-f]+)*)|(0[oO][0-7]+(_[0-7]+)*)|(0[bB][01]+(_[01]+)*)|(\\d+(_\\d+)*(e[+-]?\\d+(_\\d+)*)?)"))
  _decimal_number (:seq
                   (:choice (:choice "-" "+") :blank)
                   (:pattern "((\\d+(_\\d+)*)?[.]\\d+(_\\d+)*(e[+-]?\\d+(_\\d+)*)?)|(\\d+(_\\d+)*[.](e[+-]?\\d+(_\\d+)*)?)"))
  _bit_string (:seq (:pattern "[bBxX]'([^']|'')*'") (:repeat (:pattern "'([^']|'')*'")))
  _string_casting (:seq identifier _single_quote_string)
  bang "!"
  identifier (:choice _identifier _double_quote_string _tsql_parameter (:seq "`" _identifier "`"))
  _tsql_parameter (:seq "@" _identifier)
  _identifier (:pattern "[A-Za-z_\\u00C0-\\u017F][0-9A-Za-z_\\u00C0-\\u017F]*")}}
