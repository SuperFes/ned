# .h stays C's unless the header says otherwise: libmagic's verdict, then
# the Objective-C-only directives.
{:name "objc"
 :extensions [".m"]
 :injection-aliases ["objective-c" "objectivec"]
 :shared-extensions [".h"]
 :mime-types ["text/x-objective-c"]
 :content-pattern "^\\s*(@interface|@protocol|@implementation|#import)\\b"
 :line-comment "//"
 # The upstream queries are deltas whose first line says `inherits: c`;
 # discovery doesn't read that, so the base is named here.
 :queries {:highlights ["c/highlights.janet"
                        "objc/upstream/highlights.janet"]
           :locals ["c/locals.janet"
                    "objc/upstream/locals.janet"]
           :signatures ["c/signatures.janet"]
           :calls ["c/calls.janet"]}
 :signature-template "void __ned_sig({}) {}"
}
