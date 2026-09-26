(let . (call item: (ident) @signature.name (group) @signature.parameters)) @signature.definition

(group (ident) @parameter.name @parameter)
(group (tagged field: (ident) @parameter.name (_) @parameter.default .) @parameter.keyword)
(group (elude) @parameter.variadic)
