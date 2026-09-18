# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "markdown"
 :extras []
 :conflicts [[link_reference_definition] [link_label _line] [link_reference_definition _line]]
 :precedences [[_setext_heading1 _block] [_setext_heading2 _block] [indented_code_block _block]]
 :externals [_line_ending
             _soft_line_ending
             _block_close
             block_continuation
             _block_quote_start
             _indented_chunk_start
             atx_h1_marker
             atx_h2_marker
             atx_h3_marker
             atx_h4_marker
             atx_h5_marker
             atx_h6_marker
             setext_h1_underline
             setext_h2_underline
             _thematic_break
             _list_marker_minus
             _list_marker_plus
             _list_marker_star
             _list_marker_parenthesis
             _list_marker_dot
             _list_marker_minus_dont_interrupt
             _list_marker_plus_dont_interrupt
             _list_marker_star_dont_interrupt
             _list_marker_parenthesis_dont_interrupt
             _list_marker_dot_dont_interrupt
             _fenced_code_block_start_backtick
             _fenced_code_block_start_tilde
             _blank_line_start
             _fenced_code_block_end_backtick
             _fenced_code_block_end_tilde
             _html_block_1_start
             _html_block_1_end
             _html_block_2_start
             _html_block_3_start
             _html_block_4_start
             _html_block_5_start
             _html_block_6_start
             _html_block_7_start
             _close_block
             _no_indented_chunk
             _error
             _trigger_error
             _eof
             minus_metadata
             plus_metadata
             _pipe_table_start
             _pipe_table_line_ending]
 :inline []
 :supertypes []
 :rules
 {document (:seq
            (:choice (:choice minus_metadata plus_metadata) :blank)
            (:alias (:prec-right 0 (:repeat _block_not_section)) section)
            (:repeat section))
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
  _last_token_punctuation (:choice)
  _block (:choice _block_not_section section)
  _block_not_section (:choice
                      (:alias _setext_heading1 setext_heading)
                      (:alias _setext_heading2 setext_heading)
                      paragraph
                      indented_code_block
                      block_quote
                      thematic_break
                      list
                      fenced_code_block
                      _blank_line
                      html_block
                      link_reference_definition
                      pipe_table)
  section (:choice _section1 _section2 _section3 _section4 _section5 _section6)
  _section1 (:prec-right 0
             (:seq
              (:alias _atx_heading1 atx_heading)
              (:repeat
               (:choice
                (:alias (:choice _section6 _section5 _section4 _section3 _section2) section)
                _block_not_section))))
  _section2 (:prec-right 0
             (:seq
              (:alias _atx_heading2 atx_heading)
              (:repeat
               (:choice
                (:alias (:choice _section6 _section5 _section4 _section3) section)
                _block_not_section))))
  _section3 (:prec-right 0
             (:seq
              (:alias _atx_heading3 atx_heading)
              (:repeat
               (:choice (:alias (:choice _section6 _section5 _section4) section) _block_not_section))))
  _section4 (:prec-right 0
             (:seq
              (:alias _atx_heading4 atx_heading)
              (:repeat (:choice (:alias (:choice _section6 _section5) section) _block_not_section))))
  _section5 (:prec-right 0
             (:seq
              (:alias _atx_heading5 atx_heading)
              (:repeat (:choice (:alias _section6 section) _block_not_section))))
  _section6 (:prec-right 0 (:seq (:alias _atx_heading6 atx_heading) (:repeat _block_not_section)))
  thematic_break (:seq _thematic_break (:choice _newline _eof))
  _atx_heading1 (:prec 1 (:seq atx_h1_marker (:choice _atx_heading_content :blank) _newline))
  _atx_heading2 (:prec 1 (:seq atx_h2_marker (:choice _atx_heading_content :blank) _newline))
  _atx_heading3 (:prec 1 (:seq atx_h3_marker (:choice _atx_heading_content :blank) _newline))
  _atx_heading4 (:prec 1 (:seq atx_h4_marker (:choice _atx_heading_content :blank) _newline))
  _atx_heading5 (:prec 1 (:seq atx_h5_marker (:choice _atx_heading_content :blank) _newline))
  _atx_heading6 (:prec 1 (:seq atx_h6_marker (:choice _atx_heading_content :blank) _newline))
  _atx_heading_content (:prec 1
                        (:seq
                         (:choice _whitespace :blank)
                         (:field :heading_content (:alias _line inline))))
  _setext_heading1 (:seq
                    (:field :heading_content paragraph)
                    setext_h1_underline
                    (:choice _newline _eof))
  _setext_heading2 (:seq
                    (:field :heading_content paragraph)
                    setext_h2_underline
                    (:choice _newline _eof))
  indented_code_block (:prec-right 0
                       (:seq _indented_chunk (:repeat (:choice _indented_chunk _blank_line))))
  _indented_chunk (:seq
                   _indented_chunk_start
                   (:repeat (:choice _line _newline))
                   _block_close
                   (:choice block_continuation :blank))
  fenced_code_block (:prec-right 0
                     (:choice
                      (:seq
                       (:alias _fenced_code_block_start_backtick fenced_code_block_delimiter)
                       (:choice _whitespace :blank)
                       (:choice info_string :blank)
                       _newline
                       (:choice code_fence_content :blank)
                       (:choice
                        (:seq
                         (:alias _fenced_code_block_end_backtick fenced_code_block_delimiter)
                         _close_block
                         _newline)
                        :blank)
                       _block_close)
                      (:seq
                       (:alias _fenced_code_block_start_tilde fenced_code_block_delimiter)
                       (:choice _whitespace :blank)
                       (:choice info_string :blank)
                       _newline
                       (:choice code_fence_content :blank)
                       (:choice
                        (:seq
                         (:alias _fenced_code_block_end_tilde fenced_code_block_delimiter)
                         _close_block
                         _newline)
                        :blank)
                       _block_close)))
  code_fence_content (:repeat1 (:choice _newline _line))
  info_string (:choice
               (:seq
                language
                (:repeat
                 (:choice _line backslash_escape entity_reference numeric_character_reference)))
               (:seq
                (:repeat1 (:choice "{" "}"))
                (:choice
                 (:choice
                  (:seq
                   language
                   (:repeat
                    (:choice _line backslash_escape entity_reference numeric_character_reference)))
                  (:seq
                   _whitespace
                   (:repeat
                    (:choice _line backslash_escape entity_reference numeric_character_reference))))
                 :blank)))
  language (:prec-right 0
            (:repeat1
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
                "|"
                "~")
               (:choice _last_token_punctuation :blank))
              backslash_escape
              entity_reference
              numeric_character_reference)))
  html_block (:prec 1
              (:seq
               (:choice _whitespace :blank)
               (:choice
                _html_block_1
                _html_block_2
                _html_block_3
                _html_block_4
                _html_block_5
                _html_block_6
                _html_block_7)))
  _html_block_1 (:seq
                 _html_block_1_start
                 (:repeat (:choice _line _newline (:seq _html_block_1_end _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  _html_block_2 (:seq
                 _html_block_2_start
                 (:repeat (:choice _line _newline (:seq "-->" _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  _html_block_3 (:seq
                 _html_block_3_start
                 (:repeat (:choice _line _newline (:seq "?>" _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  _html_block_4 (:seq
                 _html_block_4_start
                 (:repeat (:choice _line _newline (:seq ">" _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  _html_block_5 (:seq
                 _html_block_5_start
                 (:repeat (:choice _line _newline (:seq "]]>" _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  _html_block_6 (:seq
                 _html_block_6_start
                 (:repeat (:choice _line _newline (:seq (:seq _newline _blank_line) _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  _html_block_7 (:seq
                 _html_block_7_start
                 (:repeat (:choice _line _newline (:seq (:seq _newline _blank_line) _close_block)))
                 _block_close
                 (:choice block_continuation :blank))
  link_reference_definition (:prec-dynamic 10
                             (:seq
                              (:choice _whitespace :blank)
                              link_label
                              ":"
                              (:choice
                               (:seq
                                (:choice _whitespace :blank)
                                (:choice
                                 (:seq _soft_line_break (:choice _whitespace :blank))
                                 :blank))
                               :blank)
                              link_destination
                              (:choice
                               (:prec-dynamic 20
                                (:seq
                                 (:choice
                                  (:seq
                                   _whitespace
                                   (:choice
                                    (:seq _soft_line_break (:choice _whitespace :blank))
                                    :blank))
                                  (:seq _soft_line_break (:choice _whitespace :blank)))
                                 (:choice _no_indented_chunk :blank)
                                 link_title))
                               :blank)
                              (:choice _newline _soft_line_break _eof)))
  _text_inline_no_link (:choice
                        _word
                        _whitespace
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
                         (:choice _last_token_punctuation :blank)))
  paragraph (:seq
             (:alias (:repeat1 (:choice _line _soft_line_break)) inline)
             (:choice _newline _eof))
  _blank_line (:seq _blank_line_start (:choice _newline _eof))
  block_quote (:seq
               (:alias _block_quote_start block_quote_marker)
               (:choice block_continuation :blank)
               (:repeat _block)
               _block_close
               (:choice block_continuation :blank))
  list (:prec-right 0 (:choice _list_plus _list_minus _list_star _list_dot _list_parenthesis))
  _list_plus (:prec-right 0 (:repeat1 (:alias _list_item_plus list_item)))
  _list_minus (:prec-right 0 (:repeat1 (:alias _list_item_minus list_item)))
  _list_star (:prec-right 0 (:repeat1 (:alias _list_item_star list_item)))
  _list_dot (:prec-right 0 (:repeat1 (:alias _list_item_dot list_item)))
  _list_parenthesis (:prec-right 0 (:repeat1 (:alias _list_item_parenthesis list_item)))
  list_marker_plus (:choice _list_marker_plus _list_marker_plus_dont_interrupt)
  list_marker_minus (:choice _list_marker_minus _list_marker_minus_dont_interrupt)
  list_marker_star (:choice _list_marker_star _list_marker_star_dont_interrupt)
  list_marker_dot (:choice _list_marker_dot _list_marker_dot_dont_interrupt)
  list_marker_parenthesis (:choice _list_marker_parenthesis _list_marker_parenthesis_dont_interrupt)
  _list_item_plus (:seq
                   list_marker_plus
                   (:choice block_continuation :blank)
                   _list_item_content
                   _block_close
                   (:choice block_continuation :blank))
  _list_item_minus (:seq
                    list_marker_minus
                    (:choice block_continuation :blank)
                    _list_item_content
                    _block_close
                    (:choice block_continuation :blank))
  _list_item_star (:seq
                   list_marker_star
                   (:choice block_continuation :blank)
                   _list_item_content
                   _block_close
                   (:choice block_continuation :blank))
  _list_item_dot (:seq
                  list_marker_dot
                  (:choice block_continuation :blank)
                  _list_item_content
                  _block_close
                  (:choice block_continuation :blank))
  _list_item_parenthesis (:seq
                          list_marker_parenthesis
                          (:choice block_continuation :blank)
                          _list_item_content
                          _block_close
                          (:choice block_continuation :blank))
  _list_item_content (:choice
                      (:prec 1
                       (:seq
                        _blank_line
                        _blank_line
                        _close_block
                        (:choice block_continuation :blank)))
                      (:repeat1 _block)
                      (:prec 1
                       (:seq
                        (:choice task_list_marker_checked task_list_marker_unchecked)
                        _whitespace
                        paragraph
                        (:repeat _block))))
  _newline (:seq _line_ending (:choice block_continuation :blank))
  _soft_line_break (:seq _soft_line_ending (:choice block_continuation :blank))
  _line (:prec-right 0
         (:repeat1
          (:choice
           _word
           _whitespace
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
  _word (:choice
         (:pattern "[^!-/:-@\\[-`\\{-~ \\t\\n\\r]+")
         (:choice (:pattern "\\[[xX]\\]") (:pattern "\\[[ \\t]\\]")))
  _whitespace (:pattern "[ \\t]+")
  task_list_marker_checked (:prec 1 (:pattern "\\[[xX]\\]"))
  task_list_marker_unchecked (:prec 1 (:pattern "\\[[ \\t]\\]"))
  pipe_table (:prec-right 0
              (:seq
               _pipe_table_start
               (:alias pipe_table_row pipe_table_header)
               _newline
               pipe_table_delimiter_row
               (:repeat (:seq _pipe_table_newline (:choice pipe_table_row :blank)))
               (:choice _newline _eof)))
  _pipe_table_newline (:seq _pipe_table_line_ending (:choice block_continuation :blank))
  pipe_table_delimiter_row (:seq
                            (:choice (:seq (:choice _whitespace :blank) "|") :blank)
                            (:repeat1
                             (:prec-right 0
                              (:seq
                               (:choice _whitespace :blank)
                               pipe_table_delimiter_cell
                               (:choice _whitespace :blank)
                               "|")))
                            (:choice _whitespace :blank)
                            (:choice
                             (:seq pipe_table_delimiter_cell (:choice _whitespace :blank))
                             :blank))
  pipe_table_delimiter_cell (:seq
                             (:choice (:alias ":" pipe_table_align_left) :blank)
                             (:repeat1 "-")
                             (:choice (:alias ":" pipe_table_align_right) :blank))
  pipe_table_row (:seq
                  (:choice (:seq (:choice _whitespace :blank) "|") :blank)
                  (:choice
                   (:seq
                    (:repeat1
                     (:prec-right 0
                      (:seq
                       (:choice
                        (:seq
                         (:choice _whitespace :blank)
                         pipe_table_cell
                         (:choice _whitespace :blank))
                        (:alias _whitespace pipe_table_cell))
                       "|")))
                    (:choice _whitespace :blank)
                    (:choice (:seq pipe_table_cell (:choice _whitespace :blank)) :blank))
                   (:seq (:choice _whitespace :blank) pipe_table_cell (:choice _whitespace :blank))))
  pipe_table_cell (:prec-right 0
                   (:seq
                    (:choice
                     _word
                     _backslash_escape
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
                       "}"
                       "~")
                      (:choice _last_token_punctuation :blank)))
                    (:repeat
                     (:choice
                      _word
                      _whitespace
                      _backslash_escape
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
                        "}"
                        "~")
                       (:choice _last_token_punctuation :blank))))))}}
