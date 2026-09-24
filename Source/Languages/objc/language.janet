# .h stays with C (the more common owner); an Objective-C header needs a
# manual mode switch until a content sniff exists.
{:name "objc"
 :extensions [".m"]
 :line-comment "//"
 # The upstream queries are deltas whose first line says `inherits: c`;
 # discovery doesn't read that, so the base is named here.
 :queries {:highlights ["c/highlights.janet"
                        "objc/upstream/highlights.janet"]
           :locals ["c/locals.janet"
                    "objc/upstream/locals.janet"]}
}
