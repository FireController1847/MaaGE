#import <Foundation/Foundation.h>
#include "../clients/files.h"
int maage_client_bundle_identity(const char *path, const char *executable, const char *version, const char *build)
{
    @autoreleasepool {
        NSString *root = [NSString stringWithUTF8String:path];
        NSDictionary *info = [NSDictionary dictionaryWithContentsOfURL:[NSURL fileURLWithPath:[root stringByAppendingPathComponent:@"Contents/Info.plist"]]];
        NSString *expected = *build ? [NSString stringWithFormat:@"%s+%s", version, build] : @(version);
        return [info[@"CFBundleIdentifier"] isKindOfClass:NSString.class] &&
            [info[@"CFBundleIdentifier"] isEqualToString:@MAAGE_CLIENT_BUNDLE_IDENTIFIER] &&
            [info[@"CFBundlePackageType"] isEqual:@"APPL"] &&
            [info[@"CFBundleExecutable"] isEqual:@(executable)] &&
            [info[@"MaaGESemanticVersion"] isEqual:expected];
    }
}
