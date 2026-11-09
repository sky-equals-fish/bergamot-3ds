#pragma once
#import <Foundation/Foundation.h>

// Validate before passing user data to the core, whose parser can log malformed
// input. Never log values. This importer deliberately supports AES keys only.
static NSString* BGValidatedAESKeys(NSData* data) {
 if(!data.length||data.length>65536)return nil;
 NSString* text=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];if(!text)return nil;
 text=[text stringByReplacingOccurrencesOfString:@"\uFEFF" withString:@""];
 NSMutableString* output=[NSMutableString stringWithString:@":AES\n"];
 NSMutableSet* names=[NSMutableSet new];
 NSRegularExpression* keyName=[NSRegularExpression regularExpressionWithPattern:@"^(slot0x[0-3][0-9a-fA-F]Key[XYN]|common[0-5]|generatorConstant|otpKey|otpIV|movableKeyY|movableCmacY|dlpKeyY|nfcKeyY|nfcIv|dlpChecksumModIv)$" options:0 error:nil];
 NSCharacterSet* whitespace=NSCharacterSet.whitespaceAndNewlineCharacterSet;
 NSCharacterSet* nonHex=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet];
 for(NSString* raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
  NSString* line=[raw stringByTrimmingCharactersInSet:whitespace];
  if(!line.length||[line hasPrefix:@"#"]||[line isEqual:@":AES"])continue;
  NSArray* parts=[line componentsSeparatedByString:@"="];if(parts.count!=2)return nil;
  NSString* name=[parts[0] stringByTrimmingCharactersInSet:whitespace];NSString* value=[parts[1] stringByTrimmingCharactersInSet:whitespace];
  if([names containsObject:name]||![keyName numberOfMatchesInString:name options:0 range:NSMakeRange(0,name.length)]||value.length!=32||[value rangeOfCharacterFromSet:nonHex].location!=NSNotFound)return nil;
  [names addObject:name];[output appendFormat:@"%@=%@\n",name,value];
 }
 return names.count?output:nil;
}
