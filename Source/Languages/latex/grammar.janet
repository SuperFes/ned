# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "latex"
 :word command_name
 :extras [_whitespace line_comment]
 :conflicts []
 :precedences []
 :externals [_trivia_raw_fi
             _trivia_raw_env_comment
             _trivia_raw_env_verbatim
             _trivia_raw_env_listing
             _trivia_raw_env_minted
             _trivia_raw_env_asy
             _trivia_raw_env_asydef
             _trivia_raw_env_pycode
             _trivia_raw_env_luacode
             _trivia_raw_env_luacode_star
             _trivia_raw_env_sagesilent
             _trivia_raw_env_sageblock]
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat _root_content)
  _whitespace (:pattern "\\s+")
  line_comment (:pattern "%[^\\r\\n]*")
  block_comment (:seq
                 (:field :begin "\\iffalse")
                 (:field :comment (:choice (:alias _trivia_raw_fi comment) :blank))
                 (:field :end (:choice "\\fi" :blank)))
  _root_content (:choice _section _paragraph _flat_content)
  _flat_content (:prec-right 0 (:choice _text_with_env_content "[" "]"))
  _text_with_env_content (:choice
                          ","
                          "="
                          comment_environment
                          verbatim_environment
                          listing_environment
                          minted_environment
                          asy_environment
                          asydef_environment
                          pycode_environment
                          luacode_environment
                          sagesilent_environment
                          sageblock_environment
                          generic_environment
                          math_environment
                          _text_content)
  _text_content (:prec-right 1
                 (:choice curly_group block_comment _command text _math_content "(" ")"))
  _section (:prec-right 0
            (:choice
             (:repeat1 part)
             (:repeat1 chapter)
             (:repeat1 section)
             (:repeat1 subsection)
             (:repeat1 subsubsection)))
  _paragraph (:prec-right 0
              (:choice (:repeat1 paragraph) (:repeat1 subparagraph) (:repeat1 enum_item)))
  _section_part (:seq (:field :toc (:choice brack_group :blank)) (:field :text curly_group))
  _part_declaration (:prec-right 0
                     (:seq
                      (:field :command (:choice "\\part" "\\part*" "\\addpart" "\\addpart*"))
                      (:choice _section_part :blank)))
  part (:prec-right -1
        (:seq
         _part_declaration
         (:repeat _flat_content)
         (:choice (:prec-right -1 _paragraph) :blank)
         (:choice
          (:prec-right 0
           (:choice
            (:repeat1 chapter)
            (:repeat1 section)
            (:repeat1 subsection)
            (:repeat1 subsubsection)))
          :blank)))
  _chapter_declaration (:prec-right 0
                        (:seq
                         (:field :command
                          (:choice "\\chapter" "\\chapter*" "\\addchap" "\\addchap*"))
                         (:choice _section_part :blank)))
  chapter (:prec-right -1
           (:seq
            _chapter_declaration
            (:repeat _flat_content)
            (:choice (:prec-right -1 _paragraph) :blank)
            (:choice
             (:prec-right 0
              (:choice (:repeat1 section) (:repeat1 subsection) (:repeat1 subsubsection)))
             :blank)))
  _section_declaration (:prec-right 0
                        (:seq
                         (:field :command (:choice "\\section" "\\section*" "\\addsec" "\\addsec*"))
                         (:choice _section_part :blank)))
  section (:prec-right -1
           (:seq
            _section_declaration
            (:repeat _flat_content)
            (:choice (:prec-right -1 _paragraph) :blank)
            (:choice
             (:prec-right 0 (:choice (:repeat1 subsection) (:repeat1 subsubsection)))
             :blank)))
  _subsection_declaration (:prec-right 0
                           (:seq
                            (:field :command (:choice "\\subsection" "\\subsection*"))
                            (:choice _section_part :blank)))
  subsection (:prec-right -1
              (:seq
               _subsection_declaration
               (:repeat _flat_content)
               (:choice (:prec-right -1 _paragraph) :blank)
               (:choice (:prec-right 0 (:repeat1 subsubsection)) :blank)))
  _subsubsection_declaration (:prec-right 0
                              (:seq
                               (:field :command (:choice "\\subsubsection" "\\subsubsection*"))
                               (:choice _section_part :blank)))
  subsubsection (:prec-right -1
                 (:seq
                  _subsubsection_declaration
                  (:repeat _flat_content)
                  (:choice (:prec-right -1 _paragraph) :blank)))
  _paragraph_declaration (:prec-right 0
                          (:seq
                           (:field :command (:choice "\\paragraph" "\\paragraph*"))
                           (:choice _section_part :blank)))
  paragraph (:prec-right -1
             (:seq
              _paragraph_declaration
              (:repeat _flat_content)
              (:choice
               (:prec-right 0 (:choice (:repeat1 subparagraph) (:repeat1 enum_item)))
               :blank)))
  _subparagraph_declaration (:prec-right 0
                             (:seq
                              (:field :command (:choice "\\subparagraph" "\\subparagraph*"))
                              (:choice _section_part :blank)))
  subparagraph (:prec-right -1
                (:seq
                 _subparagraph_declaration
                 (:repeat _flat_content)
                 (:choice (:prec-right 0 (:choice (:repeat1 enum_item))) :blank)))
  _enum_itemdeclaration (:prec-right 0
                         (:seq
                          (:field :command (:choice "\\item" "\\item*"))
                          (:field :label (:choice brack_group_text :blank))))
  enum_item (:prec-right -1 (:seq _enum_itemdeclaration (:repeat _flat_content)))
  curly_group (:seq "{" (:repeat _root_content) "}")
  curly_group_text (:seq "{" (:field :text text) "}")
  curly_group_word (:seq "{" (:field :word word) "}")
  curly_group_value (:seq "{" (:field :value value_literal) "}")
  curly_group_spec (:seq "{" (:repeat (:choice _text_content "=")) "}")
  curly_group_text_list (:seq
                         "{"
                         (:choice
                          (:seq (:field :text text) (:repeat (:seq "," (:field :text text))))
                          :blank)
                         "}")
  curly_group_label (:seq "{" (:field :label label) "}")
  curly_group_label_list (:seq
                          "{"
                          (:choice
                           (:seq (:field :label label) (:repeat (:seq "," (:field :label label))))
                           :blank)
                          "}")
  curly_group_path (:seq "{" (:field :path path) "}")
  curly_group_path_list (:seq
                         "{"
                         (:choice
                          (:seq (:field :path path) (:repeat (:seq "," (:field :path path))))
                          :blank)
                         "}")
  curly_group_uri (:seq "{" (:field :uri uri) "}")
  curly_group_command_name (:seq "{" (:field :command command_name) "}")
  curly_group_key_value (:seq
                         "{"
                         (:choice
                          (:seq
                           (:field :pair key_value_pair)
                           (:repeat (:seq "," (:field :pair key_value_pair))))
                          :blank)
                         "}")
  curly_group_glob_pattern (:seq "{" (:field :pattern glob_pattern) "}")
  curly_group_impl (:seq "{" (:repeat (:choice _text_content "[" "]" "," "=")) "}")
  curly_group_author_list (:seq
                           "{"
                           (:choice
                            (:seq
                             (:alias (:repeat1 _text_content) author)
                             (:repeat
                              (:seq
                               (:alias "\\and" command_name)
                               (:alias (:repeat1 _text_content) author))))
                            :blank)
                           "}")
  brack_group (:seq "[" (:repeat (:choice _text_with_env_content brack_group)) "]")
  brack_group_text (:seq "[" (:field :text text) "]")
  brack_group_word (:seq "[" (:field :word word) "]")
  brack_group_argc (:seq "[" (:field :value argc) "]")
  brack_group_key_value (:seq
                         "["
                         (:choice
                          (:seq
                           (:field :pair key_value_pair)
                           (:repeat (:seq "," (:field :pair key_value_pair))))
                          :blank)
                         "]")
  text (:prec-right 0
        (:repeat1
         (:field :word
          (:choice operator word placeholder delimiter block_comment _command superscript subscript))))
  word (:pattern "[^\\s\\\\%\\{\\},\\$\\[\\]\\(\\)=\\#&_\\^\\-\\+\\/\\*]+")
  placeholder (:pattern "#+\\d")
  value_literal (:pattern "(\\d+\\.)?\\d+")
  delimiter (:pattern "&")
  path (:pattern "[^\\*\\\"\\[\\]:;,\\|\\{\\}<>]+")
  uri (:pattern "[^\\[\\]\\{\\}]+")
  label (:pattern "[^\\\\\\[\\]\\{\\}\\$\\(\\)=&%\\s_\\^\\#\\~,]+")
  argc (:pattern "\\d")
  glob_pattern (:repeat1 _glob_pattern_fragment)
  _glob_pattern_fragment (:choice
                          (:seq "{" (:repeat _glob_pattern_fragment) "}")
                          (:pattern "[^\\\"\\[\\]:;\\|\\{\\}<>]+"))
  operator (:choice "+" "-" "*" "/" "<" ">" "!" "|" ":" "'")
  letter (:pattern "[^\\\\%\\{\\}\\$\\#_\\^]")
  subscript (:seq "_" (:field :subscript (:choice curly_group letter command_name)))
  superscript (:seq "^" (:field :superscript (:choice curly_group letter command_name)))
  key_value_pair (:seq (:field :key text) (:choice (:seq "=" (:field :value value)) :blank))
  value (:repeat1 (:choice _text_content brack_group))
  _math_content (:choice displayed_equation inline_formula math_delimiter text_mode)
  displayed_equation (:prec-left 0
                      (:seq (:choice "$$" "\\[") (:repeat _root_content) (:choice "$$" "\\]")))
  inline_formula (:prec-left 0
                  (:seq (:choice "$" "\\(") (:repeat _root_content) (:choice "$" "\\)")))
  _math_delimiter_part (:choice word command_name "[" "]" "(" ")" "|")
  math_delimiter (:prec-left 0
                  (:seq
                   (:field :left_command (:choice "\\left" "\\bigl" "\\Bigl" "\\biggl" "\\Biggl"))
                   (:field :left_delimiter _math_delimiter_part)
                   (:repeat _root_content)
                   (:field :right_command (:choice "\\right" "\\bigr" "\\Bigr" "\\biggr" "\\Biggr"))
                   (:field :right_delimiter _math_delimiter_part)))
  text_mode (:seq
             (:field :command (:choice "\\text" "\\intertext" "\\shortintertext"))
             (:field :content curly_group))
  begin (:prec-right 0
         (:seq
          (:field :command "\\begin")
          (:field :name curly_group_text)
          (:field :options (:choice brack_group :blank))))
  end (:prec-right 0 (:seq (:field :command "\\end") (:field :name curly_group_text)))
  generic_environment (:seq (:field :begin begin) (:repeat _root_content) (:field :end end))
  comment_environment (:seq
                       (:field :begin (:alias _comment_environment_begin begin))
                       (:field :comment (:alias _trivia_raw_env_comment comment))
                       (:field :end (:alias _comment_environment_end end)))
  _comment_environment_begin (:seq
                              (:field :command "\\begin")
                              (:field :name (:alias _comment_environment_group curly_group_text))
                              (:seq))
  _comment_environment_end (:seq
                            (:field :command "\\end")
                            (:field :name (:alias _comment_environment_group curly_group_text)))
  _comment_environment_group (:seq "{" (:field :text (:alias _comment_environment_name text)) "}")
  _comment_environment_name (:seq (:field :word (:alias "comment" word)))
  verbatim_environment (:seq
                        (:field :begin (:alias _verbatim_environment_begin begin))
                        (:field :verbatim (:alias _trivia_raw_env_verbatim comment))
                        (:field :end (:alias _verbatim_environment_end end)))
  _verbatim_environment_begin (:seq
                               (:field :command "\\begin")
                               (:field :name (:alias _verbatim_environment_group curly_group_text))
                               (:seq))
  _verbatim_environment_end (:seq
                             (:field :command "\\end")
                             (:field :name (:alias _verbatim_environment_group curly_group_text)))
  _verbatim_environment_group (:seq "{" (:field :text (:alias _verbatim_environment_name text)) "}")
  _verbatim_environment_name (:seq (:field :word (:alias "verbatim" word)))
  listing_environment (:seq
                       (:field :begin (:alias _listing_environment_begin begin))
                       (:field :code (:alias _trivia_raw_env_listing source_code))
                       (:field :end (:alias _listing_environment_end end)))
  _listing_environment_begin (:seq
                              (:field :command "\\begin")
                              (:field :name (:alias _listing_environment_group curly_group_text))
                              (:seq))
  _listing_environment_end (:seq
                            (:field :command "\\end")
                            (:field :name (:alias _listing_environment_group curly_group_text)))
  _listing_environment_group (:seq "{" (:field :text (:alias _listing_environment_name text)) "}")
  _listing_environment_name (:seq (:field :word (:alias "lstlisting" word)))
  minted_environment (:seq
                      (:field :begin (:alias _minted_environment_begin begin))
                      (:field :code (:alias _trivia_raw_env_minted source_code))
                      (:field :end (:alias _minted_environment_end end)))
  _minted_environment_begin (:seq
                             (:field :command "\\begin")
                             (:field :name (:alias _minted_environment_group curly_group_text))
                             (:seq
                              (:field :options (:choice brack_group_key_value :blank))
                              (:field :language curly_group_text)))
  _minted_environment_end (:seq
                           (:field :command "\\end")
                           (:field :name (:alias _minted_environment_group curly_group_text)))
  _minted_environment_group (:seq "{" (:field :text (:alias _minted_environment_name text)) "}")
  _minted_environment_name (:seq (:field :word (:alias "minted" word)))
  asy_environment (:seq
                   (:field :begin (:alias _asy_environment_begin begin))
                   (:field :code (:alias _trivia_raw_env_asy source_code))
                   (:field :end (:alias _asy_environment_end end)))
  _asy_environment_begin (:seq
                          (:field :command "\\begin")
                          (:field :name (:alias _asy_environment_group curly_group_text))
                          (:seq))
  _asy_environment_end (:seq
                        (:field :command "\\end")
                        (:field :name (:alias _asy_environment_group curly_group_text)))
  _asy_environment_group (:seq "{" (:field :text (:alias _asy_environment_name text)) "}")
  _asy_environment_name (:seq (:field :word (:alias "asy" word)))
  asydef_environment (:seq
                      (:field :begin (:alias _asydef_environment_begin begin))
                      (:field :code (:alias _trivia_raw_env_asydef source_code))
                      (:field :end (:alias _asydef_environment_end end)))
  _asydef_environment_begin (:seq
                             (:field :command "\\begin")
                             (:field :name (:alias _asydef_environment_group curly_group_text))
                             (:seq))
  _asydef_environment_end (:seq
                           (:field :command "\\end")
                           (:field :name (:alias _asydef_environment_group curly_group_text)))
  _asydef_environment_group (:seq "{" (:field :text (:alias _asydef_environment_name text)) "}")
  _asydef_environment_name (:seq (:field :word (:alias "asydef" word)))
  pycode_environment (:seq
                      (:field :begin (:alias _pycode_environment_begin begin))
                      (:field :code (:alias _trivia_raw_env_pycode source_code))
                      (:field :end (:alias _pycode_environment_end end)))
  _pycode_environment_begin (:seq
                             (:field :command "\\begin")
                             (:field :name (:alias _pycode_environment_group curly_group_text))
                             (:seq))
  _pycode_environment_end (:seq
                           (:field :command "\\end")
                           (:field :name (:alias _pycode_environment_group curly_group_text)))
  _pycode_environment_group (:seq "{" (:field :text (:alias _pycode_environment_name text)) "}")
  _pycode_environment_name (:seq (:field :word (:alias "pycode" word)))
  luacode_environment (:choice _luacode_environment _luacode_environment_star)
  _luacode_environment (:seq
                        (:field :begin (:alias __luacode_environment_begin begin))
                        (:field :code (:alias _trivia_raw_env_luacode source_code))
                        (:field :end (:alias __luacode_environment_end end)))
  __luacode_environment_begin (:seq
                               (:field :command "\\begin")
                               (:field :name (:alias __luacode_environment_group curly_group_text))
                               (:seq))
  __luacode_environment_end (:seq
                             (:field :command "\\end")
                             (:field :name (:alias __luacode_environment_group curly_group_text)))
  __luacode_environment_group (:seq "{" (:field :text (:alias __luacode_environment_name text)) "}")
  __luacode_environment_name (:seq (:field :word (:alias "luacode" word)))
  _luacode_environment_star (:seq
                             (:field :begin (:alias __luacode_environment_star_begin begin))
                             (:field :code (:alias _trivia_raw_env_luacode_star source_code))
                             (:field :end (:alias __luacode_environment_star_end end)))
  __luacode_environment_star_begin (:seq
                                    (:field :command "\\begin")
                                    (:field :name
                                     (:alias __luacode_environment_star_group curly_group_text))
                                    (:seq))
  __luacode_environment_star_end (:seq
                                  (:field :command "\\end")
                                  (:field :name
                                   (:alias __luacode_environment_star_group curly_group_text)))
  __luacode_environment_star_group (:seq
                                    "{"
                                    (:field :text (:alias __luacode_environment_star_name text))
                                    "}")
  __luacode_environment_star_name (:seq (:field :word (:alias "luacode*" word)))
  sagesilent_environment (:seq
                          (:field :begin (:alias _sagesilent_environment_begin begin))
                          (:field :code (:alias _trivia_raw_env_sagesilent source_code))
                          (:field :end (:alias _sagesilent_environment_end end)))
  _sagesilent_environment_begin (:seq
                                 (:field :command "\\begin")
                                 (:field :name
                                  (:alias _sagesilent_environment_group curly_group_text))
                                 (:seq))
  _sagesilent_environment_end (:seq
                               (:field :command "\\end")
                               (:field :name
                                (:alias _sagesilent_environment_group curly_group_text)))
  _sagesilent_environment_group (:seq
                                 "{"
                                 (:field :text (:alias _sagesilent_environment_name text))
                                 "}")
  _sagesilent_environment_name (:seq (:field :word (:alias "sagesilent" word)))
  sageblock_environment (:seq
                         (:field :begin (:alias _sageblock_environment_begin begin))
                         (:field :code (:alias _trivia_raw_env_sageblock source_code))
                         (:field :end (:alias _sageblock_environment_end end)))
  _sageblock_environment_begin (:seq
                                (:field :command "\\begin")
                                (:field :name
                                 (:alias _sageblock_environment_group curly_group_text))
                                (:seq))
  _sageblock_environment_end (:seq
                              (:field :command "\\end")
                              (:field :name (:alias _sageblock_environment_group curly_group_text)))
  _sageblock_environment_group (:seq
                                "{"
                                (:field :text (:alias _sageblock_environment_name text))
                                "}")
  _sageblock_environment_name (:seq (:field :word (:alias "sageblock" word)))
  math_environment (:seq
                    (:field :begin (:alias _math_environment_begin begin))
                    (:repeat _flat_content)
                    (:field :end (:alias _math_environment_end end)))
  _math_environment_begin (:seq
                           (:field :command "\\begin")
                           (:field :name (:alias _math_environment_group curly_group_text))
                           (:seq))
  _math_environment_end (:seq
                         (:field :command "\\end")
                         (:field :name (:alias _math_environment_group curly_group_text)))
  _math_environment_group (:seq "{" (:field :text (:alias _math_environment_name text)) "}")
  _math_environment_name (:seq
                          (:field :word
                           (:alias
                            (:choice
                             "math"
                             "displaymath"
                             "displaymath*"
                             "equation"
                             "equation*"
                             "multline"
                             "multline*"
                             "eqnarray"
                             "eqnarray*"
                             "align"
                             "align*"
                             "aligned"
                             "aligned*"
                             "array"
                             "array*"
                             "split"
                             "split*"
                             "alignat"
                             "alignat*"
                             "alignedat"
                             "alignedat*"
                             "gather"
                             "gather*"
                             "gathered"
                             "gathered*"
                             "flalign"
                             "flalign*")
                            word)))
  _command (:choice
            title_declaration
            author_declaration
            package_include
            class_include
            latex_include
            biblatex_include
            bibstyle_include
            bibtex_include
            graphics_include
            svg_include
            inkscape_include
            verbatim_include
            import_include
            caption
            citation
            counter_declaration
            counter_within_declaration
            counter_without_declaration
            counter_value
            counter_definition
            counter_addition
            counter_increment
            counter_typesetting
            label_definition
            label_reference
            label_reference_range
            label_number
            new_command_definition
            old_command_definition
            let_command_definition
            paired_delimiter_definition
            environment_definition
            glossary_entry_definition
            glossary_entry_reference
            acronym_definition
            acronym_reference
            theorem_definition
            color_definition
            color_set_definition
            color_reference
            tikz_library_import
            hyperlink
            changes_replaced
            todo
            generic_command)
  todo (:seq
        (:field :command todo_command_name)
        (:field :options (:choice brack_group :blank))
        (:field :arg curly_group))
  todo_command_name (:pattern "\\\\([a-zA-Z]?[a-zA-Z]?todo)")
  generic_command (:prec-right 0
                   (:seq (:field :command command_name) (:repeat (:field :arg curly_group))))
  command_name (:pattern "\\\\([^\\r\\n]|[@a-zA-Z]+\\*?)?")
  counter_declaration (:prec-right 0
                       (:seq
                        (:field :command "\\newcounter")
                        (:field :counter curly_group_word)
                        (:choice (:field :supercounter brack_group_word) :blank)))
  counter_within_declaration (:seq
                              (:field :command (:choice "\\counterwithin" "\\counterwithin*"))
                              (:field :counter curly_group_word)
                              (:field :supercounter curly_group_word))
  counter_without_declaration (:seq
                               (:field :command (:choice "\\counterwithout" "\\counterwithout*"))
                               (:field :counter curly_group_word)
                               (:field :supercounter curly_group_word))
  counter_value (:seq (:field :command "\\value") (:field :counter curly_group_word))
  counter_definition (:seq
                      (:field :command "\\setcounter")
                      (:field :counter curly_group_word)
                      (:field :value
                       (:choice curly_group_value (:seq "{" (:field :value counter_value) "}"))))
  counter_addition (:seq
                    (:field :command "\\addtocounter")
                    (:field :counter curly_group_word)
                    (:field :value
                     (:choice curly_group_value (:seq "{" (:field :value counter_value) "}"))))
  counter_increment (:seq
                     (:field :command (:choice "\\stepcounter" "\\refstepcounter"))
                     (:field :counter curly_group_word))
  counter_typesetting (:seq
                       (:field :command
                        (:choice "\\arabic" "\\alph" "\\Alph" "\\roman" "\\Roman" "\\fnsymbol"))
                       (:field :counter curly_group_word))
  title_declaration (:seq
                     (:field :command "\\title")
                     (:field :options (:choice brack_group :blank))
                     (:field :text curly_group))
  author_declaration (:seq
                      (:field :command "\\author")
                      (:field :options (:choice brack_group :blank))
                      (:field :authors curly_group_author_list))
  package_include (:seq
                   (:field :command (:choice "\\usepackage" "\\RequirePackage"))
                   (:field :options (:choice brack_group_key_value :blank))
                   (:field :paths curly_group_path_list))
  class_include (:seq
                 (:field :command "\\documentclass")
                 (:field :options (:choice brack_group_key_value :blank))
                 (:field :path curly_group_path))
  latex_include (:seq
                 (:field :command (:choice "\\include" "\\subfileinclude" "\\input" "\\subfile"))
                 (:field :path curly_group_path))
  biblatex_include (:seq
                    "\\addbibresource"
                    (:field :options (:choice brack_group_key_value :blank))
                    (:field :glob curly_group_glob_pattern))
  bibstyle_include (:seq (:field :command "\\bibliographystyle") (:field :path curly_group_path))
  bibtex_include (:seq (:field :command "\\bibliography") (:field :paths curly_group_path_list))
  graphics_include (:seq
                    (:field :command "\\includegraphics")
                    (:field :options (:choice brack_group_key_value :blank))
                    (:field :path curly_group_path))
  svg_include (:seq
               (:field :command "\\includesvg")
               (:field :options (:choice brack_group_key_value :blank))
               (:field :path curly_group_path))
  inkscape_include (:seq
                    (:field :command "\\includeinkscape")
                    (:field :options (:choice brack_group_key_value :blank))
                    (:field :path curly_group_path))
  verbatim_include (:seq
                    (:field :command (:choice "\\verbatiminput" "\\VerbatimInput"))
                    (:field :path curly_group_path))
  import_include (:seq
                  (:field :command
                   (:choice
                    "\\import"
                    "\\subimport"
                    "\\inputfrom"
                    "\\subimportfrom"
                    "\\includefrom"
                    "\\subincludefrom"))
                  (:field :directory curly_group_path)
                  (:field :file curly_group_path))
  caption (:seq
           (:field :command "\\caption")
           (:field :short (:choice brack_group :blank))
           (:field :long curly_group))
  citation (:seq
            (:field :command
             (:choice
              "\\cite"
              "\\cite*"
              "\\Cite"
              "\\nocite"
              "\\citet"
              "\\citep"
              "\\citet*"
              "\\citep*"
              "\\citeA"
              "\\citeR"
              "\\citeS"
              "\\citeyearR"
              "\\citeauthor"
              "\\citeauthor*"
              "\\Citeauthor"
              "\\Citeauthor*"
              "\\citetitle"
              "\\citetitle*"
              "\\citeyear"
              "\\citeyear*"
              "\\citedate"
              "\\citedate*"
              "\\citeurl"
              "\\fullcite"
              "\\citeyearpar"
              "\\citealt"
              "\\citealp"
              "\\citetext"
              "\\parencite"
              "\\parencite*"
              "\\Parencite"
              "\\footcite"
              "\\footfullcite"
              "\\footcitetext"
              "\\textcite"
              "\\Textcite"
              "\\smartcite"
              "\\Smartcite"
              "\\supercite"
              "\\autocite"
              "\\Autocite"
              "\\autocite*"
              "\\Autocite*"
              "\\volcite"
              "\\Volcite"
              "\\pvolcite"
              "\\Pvolcite"
              "\\fvolcite"
              "\\ftvolcite"
              "\\svolcite"
              "\\Svolcite"
              "\\tvolcite"
              "\\Tvolcite"
              "\\avolcite"
              "\\Avolcite"
              "\\notecite"
              "\\Notecite"
              "\\pnotecite"
              "\\Pnotecite"
              "\\fnotecite"))
            (:choice
             (:seq (:field :prenote brack_group) (:field :postnote (:choice brack_group :blank)))
             :blank)
            (:field :keys curly_group_text_list))
  label_definition (:seq (:field :command "\\label") (:field :name curly_group_label))
  label_reference (:seq
                   (:field :command
                    (:choice
                     "\\ref"
                     "\\eqref"
                     "\\vref"
                     "\\Vref"
                     "\\autoref"
                     "\\autoref*"
                     "\\pageref"
                     "\\pageref*"
                     "\\autopageref"
                     "\\autopageref*"
                     "\\cref"
                     "\\cref*"
                     "\\Cref"
                     "\\Cref*"
                     "\\cpageref"
                     "\\Cpageref"
                     "\\namecref"
                     "\\nameCref"
                     "\\lcnamecref"
                     "\\namecrefs"
                     "\\nameCrefs"
                     "\\lcnamecrefs"
                     "\\labelcref"
                     "\\labelcref*"
                     "\\labelcpageref"
                     "\\labelcpageref*"))
                   (:field :names curly_group_label_list))
  label_reference_range (:seq
                         (:field :command
                          (:choice
                           "\\crefrange"
                           "\\crefrange*"
                           "\\Crefrange"
                           "\\Crefrange*"
                           "\\cpagerefrange"
                           "\\Cpagerefrange"))
                         (:field :from curly_group_label)
                         (:field :to curly_group_label))
  label_number (:seq
                (:field :command "\\newlabel")
                (:field :name curly_group_label)
                (:field :number curly_group))
  new_command_definition (:choice
                          _new_command_definition
                          _newer_command_definition
                          _new_command_copy)
  _new_command_definition (:seq
                           (:field :command
                            (:choice
                             "\\newcommand"
                             "\\newcommand*"
                             "\\renewcommand"
                             "\\renewcommand*"
                             "\\providecommand"
                             "\\providecommand*"
                             "\\DeclareRobustCommand"
                             "\\DeclareRobustCommand*"
                             "\\DeclareMathOperator"
                             "\\DeclareMathOperator*"))
                           (:field :declaration (:choice curly_group_command_name command_name))
                           (:choice
                            (:seq
                             (:field :argc brack_group_argc)
                             (:field :default (:choice brack_group :blank)))
                            :blank)
                           (:field :implementation curly_group))
  _newer_command_definition (:seq
                             (:field :command
                              (:choice
                               "\\NewDocumentCommand"
                               "\\RenewDocumentCommand"
                               "\\ProvideDocumentCommand"
                               "\\DeclareDocumentCommand"
                               "\\NewExpandableDocumentCommand"
                               "\\RenewExpandableDocumentCommand"
                               "\\ProvideExpandableDocumentCommand"
                               "\\DeclareExpandableDocumentCommand"))
                             (:field :declaration (:choice curly_group_command_name command_name))
                             (:field :spec curly_group_spec)
                             (:field :implementation curly_group))
  _new_command_copy (:seq
                     (:field :command
                      (:choice "\\NewCommandCopy" "\\RenewCommandCopy" "\\DeclareCommandCopy"))
                     (:field :declaration (:choice curly_group_command_name command_name))
                     (:field :implementation curly_group_command_name))
  old_command_definition (:seq
                          (:field :command (:choice "\\def" "\\gdef" "\\edef" "\\xdef"))
                          (:field :declaration command_name))
  let_command_definition (:seq
                          (:field :command (:choice "\\let" "\\glet"))
                          (:field :declaration command_name)
                          (:choice "=" :blank)
                          (:field :implementation command_name))
  paired_delimiter_definition (:prec-right 0
                               (:seq
                                (:field :command
                                 (:choice "\\DeclarePairedDelimiter" "\\DeclarePairedDelimiterX"))
                                (:field :declaration curly_group_command_name)
                                (:field :argc (:choice brack_group_argc :blank))
                                (:field :left (:choice curly_group_impl command_name))
                                (:field :right (:choice curly_group_impl command_name))
                                (:field :body (:choice curly_group :blank))))
  environment_definition (:choice
                          _environment_definition
                          _newer_environment_definition
                          _new_environment_copy)
  _environment_definition (:seq
                           (:field :command (:choice "\\newenvironment" "\\renewenvironment"))
                           (:field :name curly_group_text)
                           (:field :argc (:choice brack_group_argc :blank))
                           (:field :begin curly_group_impl)
                           (:field :end curly_group_impl))
  _newer_environment_definition (:seq
                                 (:field :command
                                  (:choice
                                   "\\NewDocumentEnvironment"
                                   "\\RenewDocumentEnvironment"
                                   "\\ProvideDocumentEnvironment"
                                   "\\DeclareDocumentEnvironment"))
                                 (:field :name curly_group_text)
                                 (:field :spec curly_group_spec)
                                 (:field :begin curly_group_impl)
                                 (:field :end curly_group_impl))
  _new_environment_copy (:seq
                         (:field :command
                          (:choice
                           "\\NewEnvironmentCopy"
                           "\\RenewEnvironmentCopy"
                           "\\DeclareEnvironmentCopy"))
                         (:field :name curly_group_text)
                         (:field :name curly_group_text))
  glossary_entry_definition (:seq
                             (:field :command "\\newglossaryentry")
                             (:field :name curly_group_text)
                             (:field :options curly_group_key_value))
  glossary_entry_reference (:seq
                            (:field :command
                             (:choice
                              "\\gls"
                              "\\Gls"
                              "\\GLS"
                              "\\glspl"
                              "\\Glspl"
                              "\\GLSpl"
                              "\\glsdisp"
                              "\\glslink"
                              "\\glstext"
                              "\\Glstext"
                              "\\GLStext"
                              "\\glsfirst"
                              "\\Glsfirst"
                              "\\GLSfirst"
                              "\\glsplural"
                              "\\Glsplural"
                              "\\GLSplural"
                              "\\glsfirstplural"
                              "\\Glsfirstplural"
                              "\\GLSfirstplural"
                              "\\glsname"
                              "\\Glsname"
                              "\\GLSname"
                              "\\glssymbol"
                              "\\Glssymbol"
                              "\\glsdesc"
                              "\\Glsdesc"
                              "\\GLSdesc"
                              "\\glsuseri"
                              "\\Glsuseri"
                              "\\GLSuseri"
                              "\\glsuserii"
                              "\\Glsuserii"
                              "\\GLSuserii"
                              "\\glsuseriii"
                              "\\Glsuseriii"
                              "\\GLSuseriii"
                              "\\glsuseriv"
                              "\\Glsuseriv"
                              "\\GLSuseriv"
                              "\\glsuserv"
                              "\\Glsuserv"
                              "\\GLSuserv"
                              "\\glsuservi"
                              "\\Glsuservi"
                              "\\GLSuservi"))
                            (:field :options (:choice brack_group_key_value :blank))
                            (:field :name curly_group_text))
  acronym_definition (:seq
                      (:field :command "\\newacronym")
                      (:field :options (:choice brack_group_key_value :blank))
                      (:field :name curly_group_text)
                      (:field :short curly_group)
                      (:field :long curly_group))
  acronym_reference (:seq
                     (:field :command
                      (:choice
                       "\\acrshort"
                       "\\Acrshort"
                       "\\ACRshort"
                       "\\acrshortpl"
                       "\\Acrshortpl"
                       "\\ACRshortpl"
                       "\\acrlong"
                       "\\Acrlong"
                       "\\ACRlong"
                       "\\acrlongpl"
                       "\\Acrlongpl"
                       "\\ACRlongpl"
                       "\\acrfull"
                       "\\Acrfull"
                       "\\ACRfull"
                       "\\acrfullpl"
                       "\\Acrfullpl"
                       "\\ACRfullpl"
                       "\\acs"
                       "\\Acs"
                       "\\acsp"
                       "\\Acsp"
                       "\\acl"
                       "\\Acl"
                       "\\aclp"
                       "\\Aclp"
                       "\\acf"
                       "\\Acf"
                       "\\acfp"
                       "\\Acfp"
                       "\\ac"
                       "\\Ac"
                       "\\acp"
                       "\\glsentrylong"
                       "\\Glsentrylong"
                       "\\glsentrylongpl"
                       "\\Glsentrylongpl"
                       "\\glsentryshort"
                       "\\Glsentryshort"
                       "\\glsentryshortpl"
                       "\\Glsentryshortpl"
                       "\\glsentryfullpl"
                       "\\Glsentryfullpl"))
                     (:field :options (:choice brack_group_key_value :blank))
                     (:field :name curly_group_text))
  theorem_definition (:prec-right 0
                      (:seq
                       (:field :command
                        (:choice
                         "\\newtheorem"
                         "\\newtheorem*"
                         "\\declaretheorem"
                         "\\declaretheorem*"))
                       (:choice (:field :options brack_group_key_value) :blank)
                       (:field :name curly_group_text_list)
                       (:choice
                        (:choice
                         (:seq
                          (:field :title curly_group)
                          (:field :counter (:choice brack_group_text :blank)))
                         (:seq (:field :counter brack_group_text) (:field :title curly_group)))
                        :blank)))
  color_definition (:seq
                    (:field :command "\\definecolor")
                    (:choice brack_group_text :blank)
                    (:field :name curly_group_text)
                    (:field :model curly_group_text)
                    (:field :spec curly_group))
  color_set_definition (:seq
                        (:field :command "\\definecolorset")
                        (:field :ty (:choice brack_group_text :blank))
                        (:field :model curly_group_text_list)
                        (:field :head curly_group)
                        (:field :tail curly_group)
                        (:field :spec curly_group))
  color_reference (:prec-right 0
                   (:seq
                    (:field :command
                     (:choice "\\color" "\\pagecolor" "\\textcolor" "\\mathcolor" "\\colorbox"))
                    (:choice
                     (:field :name curly_group_text)
                     (:seq (:field :model brack_group_text) (:field :spec curly_group)))
                    (:choice (:field :text curly_group) :blank)))
  tikz_library_import (:seq
                       (:field :command (:choice "\\usepgflibrary" "\\usetikzlibrary"))
                       (:field :paths curly_group_path_list))
  hyperlink (:prec-right 0
             (:seq
              (:field :command (:choice "\\url" "\\href"))
              (:field :uri curly_group_uri)
              (:field :label (:choice curly_group :blank))))
  changes_replaced (:seq
                    (:field :command "\\replaced")
                    (:field :text_added curly_group)
                    (:field :text_deleted curly_group))}}
