# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "markdown_inline"
 :extras []
 :conflicts [[_closing_tag _text_base]
             [_open_tag _text_base]
             [_html_comment _text_base]
             [_processing_instruction _text_base]
             [_declaration _text_base]
             [_cdata_section _text_base]
             [_link_text_non_empty _inline_element]
             [_link_text_non_empty _inline_element_no_star]
             [_link_text_non_empty _inline_element_no_underscore]
             [_link_text_non_empty _inline_element_no_tilde]
             [_link_text _inline_element]
             [_link_text _inline_element_no_star]
             [_link_text _inline_element_no_underscore]
             [_link_text _inline_element_no_tilde]
             [_image_description _image_description_non_empty _text_base]
             [_image_shortcut_link _image_description]
             [shortcut_link _link_text]
             [link_destination link_title]
             [_link_destination_parenthesis link_title]
             [wiki_link _inline_element]
             [wiki_link _inline_element_no_star]
             [wiki_link _inline_element_no_underscore]
             [wiki_link _inline_element_no_tilde]
             [_emphasis_star _inline_element]
             [_emphasis_star _strong_emphasis_star _inline_element]
             [_emphasis_underscore _inline_element]
             [_emphasis_underscore _strong_emphasis_underscore _inline_element]
             [_strikethrough _inline_element]
             [_strong_emphasis_star _inline_element_no_star]
             [_emphasis_underscore _inline_element_no_star]
             [_emphasis_underscore _strong_emphasis_underscore _inline_element_no_star]
             [_strikethrough _inline_element_no_star]
             [_emphasis_star _inline_element_no_underscore]
             [_emphasis_star _strong_emphasis_star _inline_element_no_underscore]
             [_strong_emphasis_underscore _inline_element_no_underscore]
             [_strikethrough _inline_element_no_underscore]
             [_emphasis_star _inline_element_no_tilde]
             [_emphasis_star _strong_emphasis_star _inline_element_no_tilde]
             [_emphasis_underscore _inline_element_no_tilde]
             [_emphasis_underscore _strong_emphasis_underscore _inline_element_no_tilde]
             [_emphasis_star_no_link _inline_element_no_link]
             [_emphasis_star_no_link _strong_emphasis_star_no_link _inline_element_no_link]
             [_emphasis_underscore_no_link _inline_element_no_link]
             [_emphasis_underscore_no_link
              _strong_emphasis_underscore_no_link
              _inline_element_no_link]
             [_strikethrough_no_link _inline_element_no_link]
             [_strong_emphasis_star_no_link _inline_element_no_star]
             [_emphasis_underscore_no_link _inline_element_no_star_no_link]
             [_emphasis_underscore_no_link
              _strong_emphasis_underscore_no_link
              _inline_element_no_star_no_link]
             [_strikethrough_no_link _inline_element_no_star_no_link]
             [_emphasis_star_no_link _inline_element_no_underscore_no_link]
             [_emphasis_star_no_link
              _strong_emphasis_star_no_link
              _inline_element_no_underscore_no_link]
             [_strong_emphasis_underscore_no_link _inline_element_no_underscore]
             [_strikethrough_no_link _inline_element_no_underscore_no_link]
             [_emphasis_star_no_link _inline_element_no_tilde_no_link]
             [_emphasis_star_no_link _strong_emphasis_star_no_link _inline_element_no_tilde_no_link]
             [_emphasis_underscore_no_link _inline_element_no_tilde_no_link]
             [_emphasis_underscore_no_link
              _strong_emphasis_underscore_no_link
              _inline_element_no_tilde_no_link]]
 :precedences [[_strong_emphasis_star_no_link _inline_element_no_star_no_link]
               [_strong_emphasis_underscore_no_link _inline_element_no_underscore_no_link]
               [hard_line_break _whitespace]
               [hard_line_break _text_base]]
 :externals [_error
             _trigger_error
             _code_span_start
             _code_span_close
             _emphasis_open_star
             _emphasis_open_underscore
             _emphasis_close_star
             _emphasis_close_underscore
             _last_token_whitespace
             _last_token_punctuation
             _strikethrough_open
             _strikethrough_close
             _latex_span_start
             _latex_span_close
             _unclosed_span]
 :inline []
 :supertypes []
 :rules
 {inline (:seq (:choice _last_token_whitespace :blank) _inline)
  backslash_escape _backslash_escape
  _backslash_escape (:pattern "\\\\[!-/:-@\\[-`\\{-~]")
  entity_reference (:pattern "&(AEli|AElig|AM|AMP|Aacut|Aacute|Abreve|Acir|Acirc|Acy|Afr|Agrav|Agrave|Alpha|Amacr|And|Aogon|Aopf|ApplyFunction|Arin|Aring|Ascr|Assign|Atild|Atilde|Aum|Auml|Backslash|Barv|Barwed|Bcy|Because|Bernoullis|Beta|Bfr|Bopf|Breve|Bscr|Bumpeq|CHcy|COP|COPY|Cacute|Cap|CapitalDifferentialD|Cayleys|Ccaron|Ccedi|Ccedil|Ccirc|Cconint|Cdot|Cedilla|CenterDot|Cfr|Chi|CircleDot|CircleMinus|CirclePlus|CircleTimes|ClockwiseContourIntegral|CloseCurlyDoubleQuote|CloseCurlyQuote|Colon|Colone|Congruent|Conint|ContourIntegral|Copf|Coproduct|CounterClockwiseContourIntegral|Cross|Cscr|Cup|CupCap|DD|DDotrahd|DJcy|DScy|DZcy|Dagger|Darr|Dashv|Dcaron|Dcy|Del|Delta|Dfr|DiacriticalAcute|DiacriticalDot|DiacriticalDoubleAcute|DiacriticalGrave|DiacriticalTilde|Diamond|DifferentialD|Dopf|Dot|DotDot|DotEqual|DoubleContourIntegral|DoubleDot|DoubleDownArrow|DoubleLeftArrow|DoubleLeftRightArrow|DoubleLeftTee|DoubleLongLeftArrow|DoubleLongLeftRightArrow|DoubleLongRightArrow|DoubleRightArrow|DoubleRightTee|DoubleUpArrow|DoubleUpDownArrow|DoubleVerticalBar|DownArrow|DownArrowBar|DownArrowUpArrow|DownBreve|DownLeftRightVector|DownLeftTeeVector|DownLeftVector|DownLeftVectorBar|DownRightTeeVector|DownRightVector|DownRightVectorBar|DownTee|DownTeeArrow|Downarrow|Dscr|Dstrok|ENG|ET|ETH|Eacut|Eacute|Ecaron|Ecir|Ecirc|Ecy|Edot|Efr|Egrav|Egrave|Element|Emacr|EmptySmallSquare|EmptyVerySmallSquare|Eogon|Eopf|Epsilon|Equal|EqualTilde|Equilibrium|Escr|Esim|Eta|Eum|Euml|Exists|ExponentialE|Fcy|Ffr|FilledSmallSquare|FilledVerySmallSquare|Fopf|ForAll|Fouriertrf|Fscr|GJcy|G|GT|Gamma|Gammad|Gbreve|Gcedil|Gcirc|Gcy|Gdot|Gfr|Gg|Gopf|GreaterEqual|GreaterEqualLess|GreaterFullEqual|GreaterGreater|GreaterLess|GreaterSlantEqual|GreaterTilde|Gscr|Gt|HARDcy|Hacek|Hat|Hcirc|Hfr|HilbertSpace|Hopf|HorizontalLine|Hscr|Hstrok|HumpDownHump|HumpEqual|IEcy|IJlig|IOcy|Iacut|Iacute|Icir|Icirc|Icy|Idot|Ifr|Igrav|Igrave|Im|Imacr|ImaginaryI|Implies|Int|Integral|Intersection|InvisibleComma|InvisibleTimes|Iogon|Iopf|Iota|Iscr|Itilde|Iukcy|Ium|Iuml|Jcirc|Jcy|Jfr|Jopf|Jscr|Jsercy|Jukcy|KHcy|KJcy|Kappa|Kcedil|Kcy|Kfr|Kopf|Kscr|LJcy|L|LT|Lacute|Lambda|Lang|Laplacetrf|Larr|Lcaron|Lcedil|Lcy|LeftAngleBracket|LeftArrow|LeftArrowBar|LeftArrowRightArrow|LeftCeiling|LeftDoubleBracket|LeftDownTeeVector|LeftDownVector|LeftDownVectorBar|LeftFloor|LeftRightArrow|LeftRightVector|LeftTee|LeftTeeArrow|LeftTeeVector|LeftTriangle|LeftTriangleBar|LeftTriangleEqual|LeftUpDownVector|LeftUpTeeVector|LeftUpVector|LeftUpVectorBar|LeftVector|LeftVectorBar|Leftarrow|Leftrightarrow|LessEqualGreater|LessFullEqual|LessGreater|LessLess|LessSlantEqual|LessTilde|Lfr|Ll|Lleftarrow|Lmidot|LongLeftArrow|LongLeftRightArrow|LongRightArrow|Longleftarrow|Longleftrightarrow|Longrightarrow|Lopf|LowerLeftArrow|LowerRightArrow|Lscr|Lsh|Lstrok|Lt|Map|Mcy|MediumSpace|Mellintrf|Mfr|MinusPlus|Mopf|Mscr|Mu|NJcy|Nacute|Ncaron|Ncedil|Ncy|NegativeMediumSpace|NegativeThickSpace|NegativeThinSpace|NegativeVeryThinSpace|NestedGreaterGreater|NestedLessLess|NewLine|Nfr|NoBreak|NonBreakingSpace|Nopf|Not|NotCongruent|NotCupCap|NotDoubleVerticalBar|NotElement|NotEqual|NotEqualTilde|NotExists|NotGreater|NotGreaterEqual|NotGreaterFullEqual|NotGreaterGreater|NotGreaterLess|NotGreaterSlantEqual|NotGreaterTilde|NotHumpDownHump|NotHumpEqual|NotLeftTriangle|NotLeftTriangleBar|NotLeftTriangleEqual|NotLess|NotLessEqual|NotLessGreater|NotLessLess|NotLessSlantEqual|NotLessTilde|NotNestedGreaterGreater|NotNestedLessLess|NotPrecedes|NotPrecedesEqual|NotPrecedesSlantEqual|NotReverseElement|NotRightTriangle|NotRightTriangleBar|NotRightTriangleEqual|NotSquareSubset|NotSquareSubsetEqual|NotSquareSuperset|NotSquareSupersetEqual|NotSubset|NotSubsetEqual|NotSucceeds|NotSucceedsEqual|NotSucceedsSlantEqual|NotSucceedsTilde|NotSuperset|NotSupersetEqual|NotTilde|NotTildeEqual|NotTildeFullEqual|NotTildeTilde|NotVerticalBar|Nscr|Ntild|Ntilde|Nu|OElig|Oacut|Oacute|Ocir|Ocirc|Ocy|Odblac|Ofr|Ograv|Ograve|Omacr|Omega|Omicron|Oopf|OpenCurlyDoubleQuote|OpenCurlyQuote|Or|Oscr|Oslas|Oslash|Otild|Otilde|Otimes|Oum|Ouml|OverBar|OverBrace|OverBracket|OverParenthesis|PartialD|Pcy|Pfr|Phi|Pi|PlusMinus|Poincareplane|Popf|Pr|Precedes|PrecedesEqual|PrecedesSlantEqual|PrecedesTilde|Prime|Product|Proportion|Proportional|Pscr|Psi|QUO|QUOT|Qfr|Qopf|Qscr|RBarr|RE|REG|Racute|Rang|Rarr|Rarrtl|Rcaron|Rcedil|Rcy|Re|ReverseElement|ReverseEquilibrium|ReverseUpEquilibrium|Rfr|Rho|RightAngleBracket|RightArrow|RightArrowBar|RightArrowLeftArrow|RightCeiling|RightDoubleBracket|RightDownTeeVector|RightDownVector|RightDownVectorBar|RightFloor|RightTee|RightTeeArrow|RightTeeVector|RightTriangle|RightTriangleBar|RightTriangleEqual|RightUpDownVector|RightUpTeeVector|RightUpVector|RightUpVectorBar|RightVector|RightVectorBar|Rightarrow|Ropf|RoundImplies|Rrightarrow|Rscr|Rsh|RuleDelayed|SHCHcy|SHcy|SOFTcy|Sacute|Sc|Scaron|Scedil|Scirc|Scy|Sfr|ShortDownArrow|ShortLeftArrow|ShortRightArrow|ShortUpArrow|Sigma|SmallCircle|Sopf|Sqrt|Square|SquareIntersection|SquareSubset|SquareSubsetEqual|SquareSuperset|SquareSupersetEqual|SquareUnion|Sscr|Star|Sub|Subset|SubsetEqual|Succeeds|SucceedsEqual|SucceedsSlantEqual|SucceedsTilde|SuchThat|Sum|Sup|Superset|SupersetEqual|Supset|THOR|THORN|TRADE|TSHcy|TScy|Tab|Tau|Tcaron|Tcedil|Tcy|Tfr|Therefore|Theta|ThickSpace|ThinSpace|Tilde|TildeEqual|TildeFullEqual|TildeTilde|Topf|TripleDot|Tscr|Tstrok|Uacut|Uacute|Uarr|Uarrocir|Ubrcy|Ubreve|Ucir|Ucirc|Ucy|Udblac|Ufr|Ugrav|Ugrave|Umacr|UnderBar|UnderBrace|UnderBracket|UnderParenthesis|Union|UnionPlus|Uogon|Uopf|UpArrow|UpArrowBar|UpArrowDownArrow|UpDownArrow|UpEquilibrium|UpTee|UpTeeArrow|Uparrow|Updownarrow|UpperLeftArrow|UpperRightArrow|Upsi|Upsilon|Uring|Uscr|Utilde|Uum|Uuml|VDash|Vbar|Vcy|Vdash|Vdashl|Vee|Verbar|Vert|VerticalBar|VerticalLine|VerticalSeparator|VerticalTilde|VeryThinSpace|Vfr|Vopf|Vscr|Vvdash|Wcirc|Wedge|Wfr|Wopf|Wscr|Xfr|Xi|Xopf|Xscr|YAcy|YIcy|YUcy|Yacut|Yacute|Ycirc|Ycy|Yfr|Yopf|Yscr|Yuml|ZHcy|Zacute|Zcaron|Zcy|Zdot|ZeroWidthSpace|Zeta|Zfr|Zopf|Zscr|aacut|aacute|abreve|ac|acE|acd|acir|acirc|acut|acute|acy|aeli|aelig|af|afr|agrav|agrave|alefsym|aleph|alpha|amacr|amalg|am|amp|and|andand|andd|andslope|andv|ang|ange|angle|angmsd|angmsdaa|angmsdab|angmsdac|angmsdad|angmsdae|angmsdaf|angmsdag|angmsdah|angrt|angrtvb|angrtvbd|angsph|angst|angzarr|aogon|aopf|ap|apE|apacir|ape|apid|apos|approx|approxeq|arin|aring|ascr|ast|asymp|asympeq|atild|atilde|aum|auml|awconint|awint|bNot|backcong|backepsilon|backprime|backsim|backsimeq|barvee|barwed|barwedge|bbrk|bbrktbrk|bcong|bcy|bdquo|becaus|because|bemptyv|bepsi|bernou|beta|beth|between|bfr|bigcap|bigcirc|bigcup|bigodot|bigoplus|bigotimes|bigsqcup|bigstar|bigtriangledown|bigtriangleup|biguplus|bigvee|bigwedge|bkarow|blacklozenge|blacksquare|blacktriangle|blacktriangledown|blacktriangleleft|blacktriangleright|blank|blk12|blk14|blk34|block|bne|bnequiv|bnot|bopf|bot|bottom|bowtie|boxDL|boxDR|boxDl|boxDr|boxH|boxHD|boxHU|boxHd|boxHu|boxUL|boxUR|boxUl|boxUr|boxV|boxVH|boxVL|boxVR|boxVh|boxVl|boxVr|boxbox|boxdL|boxdR|boxdl|boxdr|boxh|boxhD|boxhU|boxhd|boxhu|boxminus|boxplus|boxtimes|boxuL|boxuR|boxul|boxur|boxv|boxvH|boxvL|boxvR|boxvh|boxvl|boxvr|bprime|breve|brvba|brvbar|bscr|bsemi|bsim|bsime|bsol|bsolb|bsolhsub|bull|bullet|bump|bumpE|bumpe|bumpeq|cacute|cap|capand|capbrcup|capcap|capcup|capdot|caps|caret|caron|ccaps|ccaron|ccedi|ccedil|ccirc|ccups|ccupssm|cdot|cedi|cedil|cemptyv|cen|cent|centerdot|cfr|chcy|check|checkmark|chi|cir|cirE|circ|circeq|circlearrowleft|circlearrowright|circledR|circledS|circledast|circledcirc|circleddash|cire|cirfnint|cirmid|cirscir|clubs|clubsuit|colon|colone|coloneq|comma|commat|comp|compfn|complement|complexes|cong|congdot|conint|copf|coprod|cop|copy|copysr|crarr|cross|cscr|csub|csube|csup|csupe|ctdot|cudarrl|cudarrr|cuepr|cuesc|cularr|cularrp|cup|cupbrcap|cupcap|cupcup|cupdot|cupor|cups|curarr|curarrm|curlyeqprec|curlyeqsucc|curlyvee|curlywedge|curre|curren|curvearrowleft|curvearrowright|cuvee|cuwed|cwconint|cwint|cylcty|dArr|dHar|dagger|daleth|darr|dash|dashv|dbkarow|dblac|dcaron|dcy|dd|ddagger|ddarr|ddotseq|de|deg|delta|demptyv|dfisht|dfr|dharl|dharr|diam|diamond|diamondsuit|diams|die|digamma|disin|div|divid|divide|divideontimes|divonx|djcy|dlcorn|dlcrop|dollar|dopf|dot|doteq|doteqdot|dotminus|dotplus|dotsquare|doublebarwedge|downarrow|downdownarrows|downharpoonleft|downharpoonright|drbkarow|drcorn|drcrop|dscr|dscy|dsol|dstrok|dtdot|dtri|dtrif|duarr|duhar|dwangle|dzcy|dzigrarr|eDDot|eDot|eacut|eacute|easter|ecaron|ecir|ecir|ecirc|ecolon|ecy|edot|ee|efDot|efr|eg|egrav|egrave|egs|egsdot|el|elinters|ell|els|elsdot|emacr|empty|emptyset|emptyv|emsp13|emsp14|emsp|eng|ensp|eogon|eopf|epar|eparsl|eplus|epsi|epsilon|epsiv|eqcirc|eqcolon|eqsim|eqslantgtr|eqslantless|equals|equest|equiv|equivDD|eqvparsl|erDot|erarr|escr|esdot|esim|eta|et|eth|eum|euml|euro|excl|exist|expectation|exponentiale|fallingdotseq|fcy|female|ffilig|fflig|ffllig|ffr|filig|fjlig|flat|fllig|fltns|fnof|fopf|forall|fork|forkv|fpartint|frac1|frac12|frac13|frac1|frac14|frac15|frac16|frac18|frac23|frac25|frac3|frac34|frac35|frac38|frac45|frac56|frac58|frac78|frasl|frown|fscr|gE|gEl|gacute|gamma|gammad|gap|gbreve|gcirc|gcy|gdot|ge|gel|geq|geqq|geqslant|ges|gescc|gesdot|gesdoto|gesdotol|gesl|gesles|gfr|gg|ggg|gimel|gjcy|gl|glE|gla|glj|gnE|gnap|gnapprox|gne|gneq|gneqq|gnsim|gopf|grave|gscr|gsim|gsime|gsiml|g|gt|gtcc|gtcir|gtdot|gtlPar|gtquest|gtrapprox|gtrarr|gtrdot|gtreqless|gtreqqless|gtrless|gtrsim|gvertneqq|gvnE|hArr|hairsp|half|hamilt|hardcy|harr|harrcir|harrw|hbar|hcirc|hearts|heartsuit|hellip|hercon|hfr|hksearow|hkswarow|hoarr|homtht|hookleftarrow|hookrightarrow|hopf|horbar|hscr|hslash|hstrok|hybull|hyphen|iacut|iacute|ic|icir|icirc|icy|iecy|iexc|iexcl|iff|ifr|igrav|igrave|ii|iiiint|iiint|iinfin|iiota|ijlig|imacr|image|imagline|imagpart|imath|imof|imped|in|incare|infin|infintie|inodot|int|intcal|integers|intercal|intlarhk|intprod|iocy|iogon|iopf|iota|iprod|iques|iquest|iscr|isin|isinE|isindot|isins|isinsv|isinv|it|itilde|iukcy|ium|iuml|jcirc|jcy|jfr|jmath|jopf|jscr|jsercy|jukcy|kappa|kappav|kcedil|kcy|kfr|kgreen|khcy|kjcy|kopf|kscr|lAarr|lArr|lAtail|lBarr|lE|lEg|lHar|lacute|laemptyv|lagran|lambda|lang|langd|langle|lap|laqu|laquo|larr|larrb|larrbfs|larrfs|larrhk|larrlp|larrpl|larrsim|larrtl|lat|latail|late|lates|lbarr|lbbrk|lbrace|lbrack|lbrke|lbrksld|lbrkslu|lcaron|lcedil|lceil|lcub|lcy|ldca|ldquo|ldquor|ldrdhar|ldrushar|ldsh|le|leftarrow|leftarrowtail|leftharpoondown|leftharpoonup|leftleftarrows|leftrightarrow|leftrightarrows|leftrightharpoons|leftrightsquigarrow|leftthreetimes|leg|leq|leqq|leqslant|les|lescc|lesdot|lesdoto|lesdotor|lesg|lesges|lessapprox|lessdot|lesseqgtr|lesseqqgtr|lessgtr|lesssim|lfisht|lfloor|lfr|lg|lgE|lhard|lharu|lharul|lhblk|ljcy|ll|llarr|llcorner|llhard|lltri|lmidot|lmoust|lmoustache|lnE|lnap|lnapprox|lne|lneq|lneqq|lnsim|loang|loarr|lobrk|longleftarrow|longleftrightarrow|longmapsto|longrightarrow|looparrowleft|looparrowright|lopar|lopf|loplus|lotimes|lowast|lowbar|loz|lozenge|lozf|lpar|lparlt|lrarr|lrcorner|lrhar|lrhard|lrm|lrtri|lsaquo|lscr|lsh|lsim|lsime|lsimg|lsqb|lsquo|lsquor|lstrok|l|lt|ltcc|ltcir|ltdot|lthree|ltimes|ltlarr|ltquest|ltrPar|ltri|ltrie|ltrif|lurdshar|luruhar|lvertneqq|lvnE|mDDot|mac|macr|male|malt|maltese|map|mapsto|mapstodown|mapstoleft|mapstoup|marker|mcomma|mcy|mdash|measuredangle|mfr|mho|micr|micro|mid|midast|midcir|middo|middot|minus|minusb|minusd|minusdu|mlcp|mldr|mnplus|models|mopf|mp|mscr|mstpos|mu|multimap|mumap|nGg|nGt|nGtv|nLeftarrow|nLeftrightarrow|nLl|nLt|nLtv|nRightarrow|nVDash|nVdash|nabla|nacute|nang|nap|napE|napid|napos|napprox|natur|natural|naturals|nbs|nbsp|nbump|nbumpe|ncap|ncaron|ncedil|ncong|ncongdot|ncup|ncy|ndash|ne|neArr|nearhk|nearr|nearrow|nedot|nequiv|nesear|nesim|nexist|nexists|nfr|ngE|nge|ngeq|ngeqq|ngeqslant|nges|ngsim|ngt|ngtr|nhArr|nharr|nhpar|ni|nis|nisd|niv|njcy|nlArr|nlE|nlarr|nldr|nle|nleftarrow|nleftrightarrow|nleq|nleqq|nleqslant|nles|nless|nlsim|nlt|nltri|nltrie|nmid|nopf|no|not|notin|notinE|notindot|notinva|notinvb|notinvc|notni|notniva|notnivb|notnivc|npar|nparallel|nparsl|npart|npolint|npr|nprcue|npre|nprec|npreceq|nrArr|nrarr|nrarrc|nrarrw|nrightarrow|nrtri|nrtrie|nsc|nsccue|nsce|nscr|nshortmid|nshortparallel|nsim|nsime|nsimeq|nsmid|nspar|nsqsube|nsqsupe|nsub|nsubE|nsube|nsubset|nsubseteq|nsubseteqq|nsucc|nsucceq|nsup|nsupE|nsupe|nsupset|nsupseteq|nsupseteqq|ntgl|ntild|ntilde|ntlg|ntriangleleft|ntrianglelefteq|ntriangleright|ntrianglerighteq|nu|num|numero|numsp|nvDash|nvHarr|nvap|nvdash|nvge|nvgt|nvinfin|nvlArr|nvle|nvlt|nvltrie|nvrArr|nvrtrie|nvsim|nwArr|nwarhk|nwarr|nwarrow|nwnear|oS|oacut|oacute|oast|ocir|ocir|ocirc|ocy|odash|odblac|odiv|odot|odsold|oelig|ofcir|ofr|ogon|ograv|ograve|ogt|ohbar|ohm|oint|olarr|olcir|olcross|oline|olt|omacr|omega|omicron|omid|ominus|oopf|opar|operp|oplus|or|orarr|ord|order|orderof|ord|ordf|ord|ordm|origof|oror|orslope|orv|oscr|oslas|oslash|osol|otild|otilde|otimes|otimesas|oum|ouml|ovbar|par|par|para|parallel|parsim|parsl|part|pcy|percnt|period|permil|perp|pertenk|pfr|phi|phiv|phmmat|phone|pi|pitchfork|piv|planck|planckh|plankv|plus|plusacir|plusb|pluscir|plusdo|plusdu|pluse|plusm|plusmn|plussim|plustwo|pm|pointint|popf|poun|pound|pr|prE|prap|prcue|pre|prec|precapprox|preccurlyeq|preceq|precnapprox|precneqq|precnsim|precsim|prime|primes|prnE|prnap|prnsim|prod|profalar|profline|profsurf|prop|propto|prsim|prurel|pscr|psi|puncsp|qfr|qint|qopf|qprime|qscr|quaternions|quatint|quest|questeq|quo|quot|rAarr|rArr|rAtail|rBarr|rHar|race|racute|radic|raemptyv|rang|rangd|range|rangle|raqu|raquo|rarr|rarrap|rarrb|rarrbfs|rarrc|rarrfs|rarrhk|rarrlp|rarrpl|rarrsim|rarrtl|rarrw|ratail|ratio|rationals|rbarr|rbbrk|rbrace|rbrack|rbrke|rbrksld|rbrkslu|rcaron|rcedil|rceil|rcub|rcy|rdca|rdldhar|rdquo|rdquor|rdsh|real|realine|realpart|reals|rect|re|reg|rfisht|rfloor|rfr|rhard|rharu|rharul|rho|rhov|rightarrow|rightarrowtail|rightharpoondown|rightharpoonup|rightleftarrows|rightleftharpoons|rightrightarrows|rightsquigarrow|rightthreetimes|ring|risingdotseq|rlarr|rlhar|rlm|rmoust|rmoustache|rnmid|roang|roarr|robrk|ropar|ropf|roplus|rotimes|rpar|rpargt|rppolint|rrarr|rsaquo|rscr|rsh|rsqb|rsquo|rsquor|rthree|rtimes|rtri|rtrie|rtrif|rtriltri|ruluhar|rx|sacute|sbquo|sc|scE|scap|scaron|sccue|sce|scedil|scirc|scnE|scnap|scnsim|scpolint|scsim|scy|sdot|sdotb|sdote|seArr|searhk|searr|searrow|sec|sect|semi|seswar|setminus|setmn|sext|sfr|sfrown|sharp|shchcy|shcy|shortmid|shortparallel|sh|shy|sigma|sigmaf|sigmav|sim|simdot|sime|simeq|simg|simgE|siml|simlE|simne|simplus|simrarr|slarr|smallsetminus|smashp|smeparsl|smid|smile|smt|smte|smtes|softcy|sol|solb|solbar|sopf|spades|spadesuit|spar|sqcap|sqcaps|sqcup|sqcups|sqsub|sqsube|sqsubset|sqsubseteq|sqsup|sqsupe|sqsupset|sqsupseteq|squ|square|squarf|squf|srarr|sscr|ssetmn|ssmile|sstarf|star|starf|straightepsilon|straightphi|strns|sub|subE|subdot|sube|subedot|submult|subnE|subne|subplus|subrarr|subset|subseteq|subseteqq|subsetneq|subsetneqq|subsim|subsub|subsup|succ|succapprox|succcurlyeq|succeq|succnapprox|succneqq|succnsim|succsim|sum|sung|sup|sup1|sup|sup2|sup|sup3|sup|supE|supdot|supdsub|supe|supedot|suphsol|suphsub|suplarr|supmult|supnE|supne|supplus|supset|supseteq|supseteqq|supsetneq|supsetneqq|supsim|supsub|supsup|swArr|swarhk|swarr|swarrow|swnwar|szli|szlig|target|tau|tbrk|tcaron|tcedil|tcy|tdot|telrec|tfr|there4|therefore|theta|thetasym|thetav|thickapprox|thicksim|thinsp|thkap|thksim|thor|thorn|tilde|time|times|timesb|timesbar|timesd|tint|toea|top|topbot|topcir|topf|topfork|tosa|tprime|trade|triangle|triangledown|triangleleft|trianglelefteq|triangleq|triangleright|trianglerighteq|tridot|trie|triminus|triplus|trisb|tritime|trpezium|tscr|tscy|tshcy|tstrok|twixt|twoheadleftarrow|twoheadrightarrow|uArr|uHar|uacut|uacute|uarr|ubrcy|ubreve|ucir|ucirc|ucy|udarr|udblac|udhar|ufisht|ufr|ugrav|ugrave|uharl|uharr|uhblk|ulcorn|ulcorner|ulcrop|ultri|umacr|um|uml|uogon|uopf|uparrow|updownarrow|upharpoonleft|upharpoonright|uplus|upsi|upsih|upsilon|upuparrows|urcorn|urcorner|urcrop|uring|urtri|uscr|utdot|utilde|utri|utrif|uuarr|uum|uuml|uwangle|vArr|vBar|vBarv|vDash|vangrt|varepsilon|varkappa|varnothing|varphi|varpi|varpropto|varr|varrho|varsigma|varsubsetneq|varsubsetneqq|varsupsetneq|varsupsetneqq|vartheta|vartriangleleft|vartriangleright|vcy|vdash|vee|veebar|veeeq|vellip|verbar|vert|vfr|vltri|vnsub|vnsup|vopf|vprop|vrtri|vscr|vsubnE|vsubne|vsupnE|vsupne|vzigzag|wcirc|wedbar|wedge|wedgeq|weierp|wfr|wopf|wp|wr|wreath|wscr|xcap|xcirc|xcup|xdtri|xfr|xhArr|xharr|xi|xlArr|xlarr|xmap|xnis|xodot|xopf|xoplus|xotime|xrArr|xrarr|xscr|xsqcup|xuplus|xutri|xvee|xwedge|yacut|yacute|yacy|ycirc|ycy|ye|yen|yfr|yicy|yopf|yscr|yucy|yum|yuml|zacute|zcaron|zcy|zdot|zeetrf|zeta|zfr|zhcy|zigrarr|zopf|zscr|zwj|zwnj);")
  numeric_character_reference (:pattern "&#([0-9]{1,7}|[xX][0-9a-fA-F]{1,6});")
  link_label (:seq
              "["
              (:repeat1
               (:choice
                _text_inline_no_link
                backslash_escape
                entity_reference
                numeric_character_reference
                _soft_line_break))
              "]")
  link_destination (:prec-dynamic 10
                    (:choice
                     (:seq
                      "<"
                      (:repeat
                       (:choice
                        _text_no_angle
                        backslash_escape
                        entity_reference
                        numeric_character_reference))
                      ">")
                     (:seq
                      (:choice
                       _word
                       (:seq
                        (:choice
                         "!"
                         "\""
                         "#"
                         "$"
                         "%"
                         "&"
                         "'"
                         "*"
                         "+"
                         ","
                         "-"
                         "."
                         "/"
                         ":"
                         ";"
                         "="
                         ">"
                         "?"
                         "@"
                         "["
                         "\\"
                         "]"
                         "^"
                         "_"
                         "`"
                         "{"
                         "|"
                         "}"
                         "~")
                        (:choice _last_token_punctuation :blank))
                       backslash_escape
                       entity_reference
                       numeric_character_reference
                       _link_destination_parenthesis)
                      (:repeat
                       (:choice
                        _word
                        (:seq
                         (:choice
                          "!"
                          "\""
                          "#"
                          "$"
                          "%"
                          "&"
                          "'"
                          "*"
                          "+"
                          ","
                          "-"
                          "."
                          "/"
                          ":"
                          ";"
                          "<"
                          "="
                          ">"
                          "?"
                          "@"
                          "["
                          "\\"
                          "]"
                          "^"
                          "_"
                          "`"
                          "{"
                          "|"
                          "}"
                          "~")
                         (:choice _last_token_punctuation :blank))
                        backslash_escape
                        entity_reference
                        numeric_character_reference
                        _link_destination_parenthesis)))))
  _link_destination_parenthesis (:seq
                                 "("
                                 (:repeat
                                  (:choice
                                   _word
                                   (:seq
                                    (:choice
                                     "!"
                                     "\""
                                     "#"
                                     "$"
                                     "%"
                                     "&"
                                     "'"
                                     "*"
                                     "+"
                                     ","
                                     "-"
                                     "."
                                     "/"
                                     ":"
                                     ";"
                                     "<"
                                     "="
                                     ">"
                                     "?"
                                     "@"
                                     "["
                                     "\\"
                                     "]"
                                     "^"
                                     "_"
                                     "`"
                                     "{"
                                     "|"
                                     "}"
                                     "~")
                                    (:choice _last_token_punctuation :blank))
                                   backslash_escape
                                   entity_reference
                                   numeric_character_reference
                                   _link_destination_parenthesis))
                                 ")")
  _text_no_angle (:choice
                  _word
                  (:seq
                   (:choice
                    "!"
                    "\""
                    "#"
                    "$"
                    "%"
                    "&"
                    "'"
                    "("
                    ")"
                    "*"
                    "+"
                    ","
                    "-"
                    "."
                    "/"
                    ":"
                    ";"
                    "="
                    "?"
                    "@"
                    "["
                    "\\"
                    "]"
                    "^"
                    "_"
                    "`"
                    "{"
                    "|"
                    "}"
                    "~")
                   (:choice _last_token_punctuation :blank))
                  _whitespace)
  link_title (:choice
              (:seq
               "\""
               (:repeat
                (:choice
                 _word
                 (:seq
                  (:choice
                   "!"
                   "#"
                   "$"
                   "%"
                   "&"
                   "'"
                   "("
                   ")"
                   "*"
                   "+"
                   ","
                   "-"
                   "."
                   "/"
                   ":"
                   ";"
                   "<"
                   "="
                   ">"
                   "?"
                   "@"
                   "["
                   "\\"
                   "]"
                   "^"
                   "_"
                   "`"
                   "{"
                   "|"
                   "}"
                   "~")
                  (:choice _last_token_punctuation :blank))
                 _whitespace
                 backslash_escape
                 entity_reference
                 numeric_character_reference
                 (:seq _soft_line_break (:choice (:seq _soft_line_break _trigger_error) :blank))))
               "\"")
              (:seq
               "'"
               (:repeat
                (:choice
                 _word
                 (:seq
                  (:choice
                   "!"
                   "\""
                   "#"
                   "$"
                   "%"
                   "&"
                   "("
                   ")"
                   "*"
                   "+"
                   ","
                   "-"
                   "."
                   "/"
                   ":"
                   ";"
                   "<"
                   "="
                   ">"
                   "?"
                   "@"
                   "["
                   "\\"
                   "]"
                   "^"
                   "_"
                   "`"
                   "{"
                   "|"
                   "}"
                   "~")
                  (:choice _last_token_punctuation :blank))
                 _whitespace
                 backslash_escape
                 entity_reference
                 numeric_character_reference
                 (:seq _soft_line_break (:choice (:seq _soft_line_break _trigger_error) :blank))))
               "'")
              (:seq
               "("
               (:repeat
                (:choice
                 _word
                 (:seq
                  (:choice
                   "!"
                   "\""
                   "#"
                   "$"
                   "%"
                   "&"
                   "'"
                   "*"
                   "+"
                   ","
                   "-"
                   "."
                   "/"
                   ":"
                   ";"
                   "<"
                   "="
                   ">"
                   "?"
                   "@"
                   "["
                   "\\"
                   "]"
                   "^"
                   "_"
                   "`"
                   "{"
                   "|"
                   "}"
                   "~")
                  (:choice _last_token_punctuation :blank))
                 _whitespace
                 backslash_escape
                 entity_reference
                 numeric_character_reference
                 (:seq _soft_line_break (:choice (:seq _soft_line_break _trigger_error) :blank))))
               ")"))
  _newline_token (:pattern "\\n|\\r\\n?")
  code_span (:seq
             (:alias _code_span_start code_span_delimiter)
             (:repeat (:choice _text_base "[" "]" _soft_line_break _html_tag))
             (:alias _code_span_close code_span_delimiter))
  latex_block (:seq
               (:alias _latex_span_start latex_span_delimiter)
               (:repeat (:choice _text_base "[" "]" _soft_line_break _html_tag backslash_escape))
               (:alias _latex_span_close latex_span_delimiter))
  _link_text (:prec-dynamic 10 (:choice _link_text_non_empty (:seq "[" "]")))
  _link_text_non_empty (:seq "[" (:alias _inline_no_link link_text) "]")
  shortcut_link (:prec-dynamic 10 _link_text_non_empty)
  full_reference_link (:prec-dynamic 20 (:seq _link_text link_label))
  collapsed_reference_link (:prec-dynamic 10 (:seq _link_text "[" "]"))
  inline_link (:prec-dynamic 10
               (:seq
                _link_text
                "("
                (:repeat (:choice _whitespace _soft_line_break))
                (:choice
                 (:seq
                  (:choice
                   (:seq
                    link_destination
                    (:choice
                     (:seq (:repeat1 (:choice _whitespace _soft_line_break)) link_title)
                     :blank))
                   link_title)
                  (:repeat (:choice _whitespace _soft_line_break)))
                 :blank)
                ")"))
  wiki_link (:prec-dynamic 20
             (:seq
              "["
              "["
              (:alias _wiki_link_destination link_destination)
              (:choice (:seq "|" (:alias _wiki_link_text link_text)) :blank)
              "]"
              "]"))
  _wiki_link_destination (:repeat1
                          (:choice
                           _word
                           (:seq
                            (:choice
                             "!"
                             "\""
                             "#"
                             "$"
                             "%"
                             "&"
                             "'"
                             "("
                             ")"
                             "*"
                             "+"
                             ","
                             "-"
                             "."
                             "/"
                             ":"
                             ";"
                             "<"
                             "="
                             ">"
                             "?"
                             "@"
                             "\\"
                             "^"
                             "_"
                             "`"
                             "{"
                             "}"
                             "~")
                            (:choice _last_token_punctuation :blank))
                           _whitespace))
  _wiki_link_text (:repeat1
                   (:choice
                    _word
                    (:seq
                     (:choice
                      "!"
                      "\""
                      "#"
                      "$"
                      "%"
                      "&"
                      "'"
                      "("
                      ")"
                      "*"
                      "+"
                      ","
                      "-"
                      "."
                      "/"
                      ":"
                      ";"
                      "<"
                      "="
                      ">"
                      "?"
                      "@"
                      "\\"
                      "^"
                      "_"
                      "`"
                      "{"
                      "|"
                      "}"
                      "~")
                     (:choice _last_token_punctuation :blank))
                    _whitespace))
  image (:choice
         _image_inline_link
         _image_shortcut_link
         _image_full_reference_link
         _image_collapsed_reference_link)
  _image_inline_link (:prec-dynamic 10
                      (:seq
                       _image_description
                       "("
                       (:repeat (:choice _whitespace _soft_line_break))
                       (:choice
                        (:seq
                         (:choice
                          (:seq
                           link_destination
                           (:choice
                            (:seq (:repeat1 (:choice _whitespace _soft_line_break)) link_title)
                            :blank))
                          link_title)
                         (:repeat (:choice _whitespace _soft_line_break)))
                        :blank)
                       ")"))
  _image_shortcut_link (:prec-dynamic 30 _image_description_non_empty)
  _image_full_reference_link (:prec-dynamic 10 (:seq _image_description link_label))
  _image_collapsed_reference_link (:prec-dynamic 10 (:seq _image_description "[" "]"))
  _image_description (:prec-dynamic 30
                      (:choice _image_description_non_empty (:seq "!" "[" (:prec 1 "]"))))
  _image_description_non_empty (:seq "!" "[" (:alias _inline image_description) (:prec 1 "]"))
  uri_autolink (:pattern "<[a-zA-Z][a-zA-Z0-9+\\.\\-][a-zA-Z0-9+\\.\\-]*:[^ \\t\\r\\n<>]*>")
  email_autolink (:pattern "<[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*>")
  _html_tag (:choice
             _open_tag
             _closing_tag
             _html_comment
             _processing_instruction
             _declaration
             _cdata_section)
  _open_tag (:prec-dynamic 100
             (:seq
              "<"
              _tag_name
              (:repeat _attribute)
              (:repeat (:choice _whitespace _soft_line_break))
              (:choice "/" :blank)
              ">"))
  _closing_tag (:prec-dynamic 100
                (:seq "<" "/" _tag_name (:repeat (:choice _whitespace _soft_line_break)) ">"))
  _tag_name (:seq _word_no_digit (:repeat (:choice _word_no_digit _digits "-")))
  _attribute (:seq
              (:repeat1 (:choice _whitespace _soft_line_break))
              _attribute_name
              (:repeat (:choice _whitespace _soft_line_break))
              "="
              (:repeat (:choice _whitespace _soft_line_break))
              _attribute_value)
  _attribute_name (:pattern "[a-zA-Z_:][a-zA-Z0-9_\\.:\\-]*")
  _attribute_value (:choice
                    (:pattern "[^ \\t\\r\\n\"'=<>`]+")
                    (:seq
                     "'"
                     (:repeat
                      (:choice
                       _word
                       _whitespace
                       _soft_line_break
                       (:seq
                        (:choice
                         "!"
                         "\""
                         "#"
                         "$"
                         "%"
                         "&"
                         "("
                         ")"
                         "*"
                         "+"
                         ","
                         "-"
                         "."
                         "/"
                         ":"
                         ";"
                         "<"
                         "="
                         ">"
                         "?"
                         "@"
                         "["
                         "\\"
                         "]"
                         "^"
                         "_"
                         "`"
                         "{"
                         "|"
                         "}"
                         "~")
                        (:choice _last_token_punctuation :blank))))
                     "'")
                    (:seq
                     "\""
                     (:repeat
                      (:choice
                       _word
                       _whitespace
                       _soft_line_break
                       (:seq
                        (:choice
                         "!"
                         "#"
                         "$"
                         "%"
                         "&"
                         "'"
                         "("
                         ")"
                         "*"
                         "+"
                         ","
                         "-"
                         "."
                         "/"
                         ":"
                         ";"
                         "<"
                         "="
                         ">"
                         "?"
                         "@"
                         "["
                         "\\"
                         "]"
                         "^"
                         "_"
                         "`"
                         "{"
                         "|"
                         "}"
                         "~")
                        (:choice _last_token_punctuation :blank))))
                     "\""))
  _html_comment (:prec-dynamic 100
                 (:seq
                  "<!--"
                  (:choice
                   (:seq
                    (:choice
                     _word
                     _whitespace
                     _soft_line_break
                     (:seq
                      (:choice
                       "!"
                       "\""
                       "#"
                       "$"
                       "%"
                       "&"
                       "'"
                       "("
                       ")"
                       "*"
                       "+"
                       ","
                       "."
                       "/"
                       ":"
                       ";"
                       "<"
                       "="
                       "?"
                       "@"
                       "["
                       "\\"
                       "]"
                       "^"
                       "_"
                       "`"
                       "{"
                       "|"
                       "}"
                       "~")
                      (:choice _last_token_punctuation :blank))
                     (:seq
                      "-"
                      (:seq
                       (:choice
                        "!"
                        "\""
                        "#"
                        "$"
                        "%"
                        "&"
                        "'"
                        "("
                        ")"
                        "*"
                        "+"
                        ","
                        "-"
                        "."
                        "/"
                        ":"
                        ";"
                        "<"
                        "="
                        "?"
                        "@"
                        "["
                        "\\"
                        "]"
                        "^"
                        "_"
                        "`"
                        "{"
                        "|"
                        "}"
                        "~")
                       (:choice _last_token_punctuation :blank))))
                    (:repeat
                     (:prec-right 0
                      (:choice
                       _word
                       _whitespace
                       _soft_line_break
                       (:seq
                        (:choice
                         "!"
                         "\""
                         "#"
                         "$"
                         "%"
                         "&"
                         "'"
                         "("
                         ")"
                         "*"
                         "+"
                         ","
                         "."
                         "/"
                         ":"
                         ";"
                         "<"
                         "="
                         ">"
                         "?"
                         "@"
                         "["
                         "\\"
                         "]"
                         "^"
                         "_"
                         "`"
                         "{"
                         "|"
                         "}"
                         "~")
                        (:choice _last_token_punctuation :blank))
                       (:seq
                        "-"
                        (:choice
                         _word
                         _whitespace
                         _soft_line_break
                         (:seq
                          (:choice
                           "!"
                           "\""
                           "#"
                           "$"
                           "%"
                           "&"
                           "'"
                           "("
                           ")"
                           "*"
                           "+"
                           ","
                           "."
                           "/"
                           ":"
                           ";"
                           "<"
                           "="
                           ">"
                           "?"
                           "@"
                           "["
                           "\\"
                           "]"
                           "^"
                           "_"
                           "`"
                           "{"
                           "|"
                           "}"
                           "~")
                          (:choice _last_token_punctuation :blank))))))))
                   :blank)
                  "-->"))
  _processing_instruction (:prec-dynamic 100
                           (:seq
                            "<?"
                            (:repeat
                             (:prec-right 0
                              (:choice
                               _word
                               _whitespace
                               _soft_line_break
                               (:seq
                                (:choice
                                 "!"
                                 "\""
                                 "#"
                                 "$"
                                 "%"
                                 "&"
                                 "'"
                                 "("
                                 ")"
                                 "*"
                                 "+"
                                 ","
                                 "-"
                                 "."
                                 "/"
                                 ":"
                                 ";"
                                 "<"
                                 "="
                                 ">"
                                 "?"
                                 "@"
                                 "["
                                 "\\"
                                 "]"
                                 "^"
                                 "_"
                                 "`"
                                 "{"
                                 "|"
                                 "}"
                                 "~")
                                (:choice _last_token_punctuation :blank)))))
                            "?>"))
  _declaration (:prec-dynamic 100
                (:seq
                 (:pattern "<![A-Z]+")
                 (:choice _whitespace _soft_line_break)
                 (:repeat
                  (:prec-right 0
                   (:choice
                    _word
                    _whitespace
                    _soft_line_break
                    (:seq
                     (:choice
                      "!"
                      "\""
                      "#"
                      "$"
                      "%"
                      "&"
                      "'"
                      "("
                      ")"
                      "*"
                      "+"
                      ","
                      "-"
                      "."
                      "/"
                      ":"
                      ";"
                      "<"
                      "="
                      "?"
                      "@"
                      "["
                      "\\"
                      "]"
                      "^"
                      "_"
                      "`"
                      "{"
                      "|"
                      "}"
                      "~")
                     (:choice _last_token_punctuation :blank)))))
                 ">"))
  _cdata_section (:prec-dynamic 100
                  (:seq
                   "<![CDATA["
                   (:repeat
                    (:prec-right 0
                     (:choice
                      _word
                      _whitespace
                      _soft_line_break
                      (:seq
                       (:choice
                        "!"
                        "\""
                        "#"
                        "$"
                        "%"
                        "&"
                        "'"
                        "("
                        ")"
                        "*"
                        "+"
                        ","
                        "-"
                        "."
                        "/"
                        ":"
                        ";"
                        "<"
                        "="
                        ">"
                        "?"
                        "@"
                        "["
                        "\\"
                        "]"
                        "^"
                        "_"
                        "`"
                        "{"
                        "|"
                        "}"
                        "~")
                       (:choice _last_token_punctuation :blank)))))
                   "]]>"))
  hard_line_break (:seq (:choice "\\" _whitespace_ge_2) _soft_line_break)
  _text (:choice
         _word
         (:seq
          (:choice
           "!"
           "\""
           "#"
           "$"
           "%"
           "&"
           "'"
           "("
           ")"
           "*"
           "+"
           ","
           "-"
           "."
           "/"
           ":"
           ";"
           "<"
           "="
           ">"
           "?"
           "@"
           "["
           "\\"
           "]"
           "^"
           "_"
           "`"
           "{"
           "|"
           "}"
           "~")
          (:choice _last_token_punctuation :blank))
         _whitespace)
  _whitespace_ge_2 (:pattern "\\t| [ \\t]+")
  _whitespace (:seq
               (:choice _whitespace_ge_2 (:pattern " "))
               (:choice _last_token_whitespace :blank))
  _word (:choice _word_no_digit _digits)
  _word_no_digit (:pattern "[^!-/:-@\\[-`\\{-~ \\t\\n\\r0-9]+(_+[^!-/:-@\\[-`\\{-~ \\t\\n\\r0-9]+)*")
  _digits (:pattern "[0-9][0-9_]*")
  _soft_line_break (:seq _newline_token (:choice _last_token_whitespace :blank))
  _inline_base (:prec-right 0
                (:repeat1
                 (:choice
                  image
                  _soft_line_break
                  backslash_escape
                  hard_line_break
                  uri_autolink
                  email_autolink
                  entity_reference
                  numeric_character_reference
                  latex_block
                  code_span
                  (:alias _html_tag html_tag)
                  _text_base
                  (:choice)
                  _unclosed_span)))
  _text_base (:choice
              _word
              (:seq
               (:choice
                "!"
                "\""
                "#"
                "$"
                "%"
                "&"
                "'"
                "("
                ")"
                "*"
                "+"
                ","
                "-"
                "."
                "/"
                ":"
                ";"
                "<"
                "="
                ">"
                "?"
                "@"
                "\\"
                "^"
                "_"
                "`"
                "{"
                "|"
                "}"
                "~")
               (:choice _last_token_punctuation :blank))
              _whitespace
              "<!--"
              (:pattern "<![A-Z]+")
              "<?"
              "<![CDATA[")
  _text_inline_no_link (:choice
                        _text_base
                        _emphasis_open_star
                        _emphasis_open_underscore
                        _unclosed_span)
  _inline_element (:choice
                   _inline_base
                   (:alias _emphasis_star emphasis)
                   (:alias _strong_emphasis_star strong_emphasis)
                   (:alias _emphasis_underscore emphasis)
                   (:alias _strong_emphasis_underscore strong_emphasis)
                   (:alias _strikethrough strikethrough)
                   _emphasis_open_star
                   _emphasis_open_underscore
                   _strikethrough_open
                   shortcut_link
                   full_reference_link
                   collapsed_reference_link
                   inline_link
                   (:seq (:choice "[" "]") (:choice _last_token_punctuation :blank)))
  _inline (:repeat1 _inline_element)
  _inline_element_no_star (:choice
                           _inline_base
                           (:alias _emphasis_star emphasis)
                           (:alias _strong_emphasis_star strong_emphasis)
                           (:alias _emphasis_underscore emphasis)
                           (:alias _strong_emphasis_underscore strong_emphasis)
                           (:alias _strikethrough strikethrough)
                           _emphasis_open_underscore
                           _strikethrough_open
                           shortcut_link
                           full_reference_link
                           collapsed_reference_link
                           inline_link
                           (:seq (:choice "[" "]") (:choice _last_token_punctuation :blank)))
  _inline_no_star (:repeat1 _inline_element_no_star)
  _inline_element_no_underscore (:choice
                                 _inline_base
                                 (:alias _emphasis_star emphasis)
                                 (:alias _strong_emphasis_star strong_emphasis)
                                 (:alias _emphasis_underscore emphasis)
                                 (:alias _strong_emphasis_underscore strong_emphasis)
                                 (:alias _strikethrough strikethrough)
                                 _emphasis_open_star
                                 _strikethrough_open
                                 shortcut_link
                                 full_reference_link
                                 collapsed_reference_link
                                 inline_link
                                 (:seq (:choice "[" "]") (:choice _last_token_punctuation :blank)))
  _inline_no_underscore (:repeat1 _inline_element_no_underscore)
  _inline_element_no_tilde (:choice
                            _inline_base
                            (:alias _emphasis_star emphasis)
                            (:alias _strong_emphasis_star strong_emphasis)
                            (:alias _emphasis_underscore emphasis)
                            (:alias _strong_emphasis_underscore strong_emphasis)
                            (:alias _strikethrough strikethrough)
                            _emphasis_open_star
                            _emphasis_open_underscore
                            shortcut_link
                            full_reference_link
                            collapsed_reference_link
                            inline_link
                            (:seq (:choice "[" "]") (:choice _last_token_punctuation :blank)))
  _inline_no_tilde (:repeat1 _inline_element_no_tilde)
  _strikethrough (:prec-dynamic 1
                  (:seq
                   (:alias _strikethrough_open emphasis_delimiter)
                   (:choice _last_token_punctuation :blank)
                   _inline_no_tilde
                   (:alias _strikethrough_close emphasis_delimiter)))
  _emphasis_star (:prec-dynamic 1
                  (:seq
                   (:alias _emphasis_open_star emphasis_delimiter)
                   (:choice _last_token_punctuation :blank)
                   _inline_no_star
                   (:alias _emphasis_close_star emphasis_delimiter)))
  _strong_emphasis_star (:prec-dynamic 2
                         (:seq
                          (:alias _emphasis_open_star emphasis_delimiter)
                          _emphasis_star
                          (:alias _emphasis_close_star emphasis_delimiter)))
  _emphasis_underscore (:prec-dynamic 1
                        (:seq
                         (:alias _emphasis_open_underscore emphasis_delimiter)
                         (:choice _last_token_punctuation :blank)
                         _inline_no_underscore
                         (:alias _emphasis_close_underscore emphasis_delimiter)))
  _strong_emphasis_underscore (:prec-dynamic 2
                               (:seq
                                (:alias _emphasis_open_underscore emphasis_delimiter)
                                _emphasis_underscore
                                (:alias _emphasis_close_underscore emphasis_delimiter)))
  _inline_element_no_link (:choice
                           _inline_base
                           (:alias _emphasis_star_no_link emphasis)
                           (:alias _strong_emphasis_star_no_link strong_emphasis)
                           (:alias _emphasis_underscore_no_link emphasis)
                           (:alias _strong_emphasis_underscore_no_link strong_emphasis)
                           (:alias _strikethrough_no_link strikethrough)
                           _emphasis_open_star
                           _emphasis_open_underscore
                           _strikethrough_open)
  _inline_no_link (:repeat1 _inline_element_no_link)
  _inline_element_no_star_no_link (:choice
                                   _inline_base
                                   (:alias _emphasis_star_no_link emphasis)
                                   (:alias _strong_emphasis_star_no_link strong_emphasis)
                                   (:alias _emphasis_underscore_no_link emphasis)
                                   (:alias _strong_emphasis_underscore_no_link strong_emphasis)
                                   (:alias _strikethrough_no_link strikethrough)
                                   _emphasis_open_underscore
                                   _strikethrough_open)
  _inline_no_star_no_link (:repeat1 _inline_element_no_star_no_link)
  _inline_element_no_underscore_no_link (:choice
                                         _inline_base
                                         (:alias _emphasis_star_no_link emphasis)
                                         (:alias _strong_emphasis_star_no_link strong_emphasis)
                                         (:alias _emphasis_underscore_no_link emphasis)
                                         (:alias
                                          _strong_emphasis_underscore_no_link
                                          strong_emphasis)
                                         (:alias _strikethrough_no_link strikethrough)
                                         _emphasis_open_star
                                         _strikethrough_open)
  _inline_no_underscore_no_link (:repeat1 _inline_element_no_underscore_no_link)
  _inline_element_no_tilde_no_link (:choice
                                    _inline_base
                                    (:alias _emphasis_star_no_link emphasis)
                                    (:alias _strong_emphasis_star_no_link strong_emphasis)
                                    (:alias _emphasis_underscore_no_link emphasis)
                                    (:alias _strong_emphasis_underscore_no_link strong_emphasis)
                                    (:alias _strikethrough_no_link strikethrough)
                                    _emphasis_open_star
                                    _emphasis_open_underscore)
  _inline_no_tilde_no_link (:repeat1 _inline_element_no_tilde_no_link)
  _strikethrough_no_link (:prec-dynamic 1
                          (:seq
                           (:alias _strikethrough_open emphasis_delimiter)
                           (:choice _last_token_punctuation :blank)
                           _inline_no_tilde_no_link
                           (:alias _strikethrough_close emphasis_delimiter)))
  _emphasis_star_no_link (:prec-dynamic 1
                          (:seq
                           (:alias _emphasis_open_star emphasis_delimiter)
                           (:choice _last_token_punctuation :blank)
                           _inline_no_star_no_link
                           (:alias _emphasis_close_star emphasis_delimiter)))
  _strong_emphasis_star_no_link (:prec-dynamic 2
                                 (:seq
                                  (:alias _emphasis_open_star emphasis_delimiter)
                                  _emphasis_star_no_link
                                  (:alias _emphasis_close_star emphasis_delimiter)))
  _emphasis_underscore_no_link (:prec-dynamic 1
                                (:seq
                                 (:alias _emphasis_open_underscore emphasis_delimiter)
                                 (:choice _last_token_punctuation :blank)
                                 _inline_no_underscore_no_link
                                 (:alias _emphasis_close_underscore emphasis_delimiter)))
  _strong_emphasis_underscore_no_link (:prec-dynamic 2
                                       (:seq
                                        (:alias _emphasis_open_underscore emphasis_delimiter)
                                        _emphasis_underscore_no_link
                                        (:alias _emphasis_close_underscore emphasis_delimiter)))}}
