{:name "cuda"
 :extensions [".cu" ".cuh"]
 :line-comment "//"
 # The upstream query is a delta whose first line says `inherits: cpp`;
 # discovery doesn't read that, so the base is named here.
 :queries {:highlights ["cpp/highlights.janet"
                        "cuda/upstream/highlights.janet"]}
}
