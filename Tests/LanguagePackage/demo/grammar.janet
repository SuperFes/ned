# The out-of-tree language package the tests load: a grammar whose one
# external token (line_end) comes from the scanner library beside it
# (DemoScanner.cpp), so the whole grammar.janet + :scanner-library path is
# exercised without any bundled language.

{:name "demo"
 :extras [(:pattern "[ \\t]")]
 :externals [line_end]
 :rules {source_file (:repeat item)
         item (:seq word line_end)
         word (:pattern "[a-z]+")}}
