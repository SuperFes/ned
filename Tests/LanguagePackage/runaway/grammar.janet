# A grammar whose scanner (RunawayScanner.cpp) returns a zero-width token
# a repetition absorbs, without changing its state: the shape that looped
# the engine forever on a Crystal heredoc line holding only "\".

{:name "runaway"
 :extras [(:pattern "[ \\t\\n]")]
 :externals [gap]
 :rules {source_file (:repeat item)
         item (:choice word gap)
         word (:pattern "[a-z]+")}}
