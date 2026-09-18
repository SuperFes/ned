# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "diff"
 :extras [(:pattern "[\\t\\f\\v ]+")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source (:seq
          (:repeat (:choice block (:seq (:choice _line :blank) (:pattern "\\r?\\n"))))
          (:choice _line :blank))
  _line (:choice
         file_change
         binary_change
         index
         similarity
         dissimilarity
         old_file
         new_file
         location
         addition
         deletion
         change
         context
         comment
         special
         unrecognized)
  block (:prec-right 0
         (:seq
          command
          (:pattern "\\r?\\n")
          (:repeat
           (:seq
            (:choice file_change binary_change index similarity dissimilarity)
            (:pattern "\\r?\\n")))
          (:choice
           (:choice
            (:seq old_file (:pattern "\\r?\\n") new_file (:pattern "\\r?\\n") hunks)
            binary_patch)
           :blank)))
  binary_patch (:prec-right 0
                (:seq
                 (:seq (:token-immediate "GIT") "binary" "patch")
                 (:pattern "\\r?\\n")
                 (:field :forward binary_hunk)
                 (:choice (:field :reverse binary_hunk) :blank)))
  binary_hunk (:prec-right 0
               (:seq
                (:choice (:token-immediate "literal") (:token-immediate "delta"))
                (:alias (:pattern "\\d+") size)
                (:pattern "\\r?\\n")
                payload
                (:pattern "\\r?\\n")
                (:prec-right 0 (:repeat (:pattern "\\r?\\n")))))
  payload (:token-immediate
           (:pattern "[A-Za-z][0-9A-Za-z!#$%&()*+\\-;<=>?@^_`{|}~]+(?:\\r?\\n[A-Za-z][0-9A-Za-z!#$%&()*+\\-;<=>?@^_`{|}~]+)*"))
  hunks (:prec-right 0 (:repeat1 hunk))
  hunk (:prec-right 0
        (:seq
         (:field :location location)
         (:pattern "\\r?\\n")
         (:choice (:field :changes changes) :blank)))
  changes (:prec-right 0
           (:repeat1
            (:seq
             (:choice
              (:alias _hunk_addition addition)
              (:alias _hunk_deletion deletion)
              addition
              deletion
              change
              context
              special
              unrecognized)
             (:prec-right 0 (:repeat1 (:pattern "\\r?\\n"))))))
  command (:seq (:token-immediate "diff") (:alias (:pattern "[-\\w]+") argument) filename)
  file_change (:choice
               (:seq (:choice "new" "deleted") "file" "mode" mode)
               (:seq (:choice "new" "old") "mode" mode)
               (:seq (:choice "rename" "copy") (:choice "from" "to") filename))
  binary_change (:seq (:token-immediate "Binary") "files" filename "and" filename "differ")
  index (:seq (:token-immediate "index") commit ".." commit (:choice mode :blank))
  similarity (:seq (:token-immediate "similarity") "index" (:alias (:pattern "\\d+") score) "%")
  dissimilarity (:seq
                 (:token-immediate "dissimilarity")
                 "index"
                 (:alias (:pattern "\\d+") score)
                 "%")
  old_file (:seq (:token-immediate "---") filename)
  new_file (:seq (:token-immediate "+++") filename)
  location (:seq
            (:token-immediate "@@")
            linerange
            linerange
            "@@"
            (:choice (:pattern "[^\\r\\n]+") :blank))
  _hunk_addition (:seq (:token-immediate "+++") (:pattern "[^\\r\\n]+"))
  _hunk_deletion (:seq (:token-immediate "---") (:pattern "[^\\r\\n]+"))
  addition (:choice
            (:seq (:token-immediate "+") (:choice (:pattern "[^\\r\\n]+") :blank))
            (:seq (:token-immediate "++") (:choice (:pattern "[^\\r\\n]+") :blank))
            (:seq (:token-immediate "+++"))
            (:seq (:token-immediate "++++") (:choice (:pattern "[^\\r\\n]+") :blank))
            (:seq (:token-immediate ">") (:choice (:pattern "[^\\r\\n]+") :blank)))
  deletion (:choice
            (:seq (:token-immediate "-") (:choice (:pattern "[^\\r\\n]+") :blank))
            (:seq (:token-immediate "--") (:choice (:pattern "[^\\r\\n]+") :blank))
            (:seq (:token-immediate "---"))
            (:seq (:token-immediate "----") (:choice (:pattern "[^\\r\\n]+") :blank))
            (:seq (:token-immediate "<") (:choice (:pattern "[^\\r\\n]+") :blank)))
  change (:seq (:token-immediate "!") (:choice (:pattern "[^\\r\\n]+") :blank))
  context (:seq (:token-immediate " ") (:choice (:pattern "[^\\r\\n]+") :blank))
  comment (:seq (:token-immediate "#") (:choice (:pattern "[^\\r\\n]+") :blank))
  special (:seq (:token-immediate "\\") (:choice (:pattern "[^\\r\\n]+") :blank))
  unrecognized (:token (:prec -1 (:pattern "[^\\r\\n]+")))
  linerange (:pattern "[-\\+]\\d+(,\\d+)?")
  filename (:repeat1 (:pattern "\\S+"))
  commit (:pattern "[a-f0-9]{4,64}")
  mode (:pattern "\\d+")}}
