@implementation Foo
- (void)f {
    [self doThing:a
             with:b
          andMore:[x y:1
                     z:2]];
    [self a:x
        longerKeyword:y];
    [self
        doThing:a];
    [self doThing:a with:b];
}
@end
