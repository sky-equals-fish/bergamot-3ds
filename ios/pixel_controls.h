#pragma once
#include "display_layout.hpp"
// CTRSkinInput remains the existing UI/runtime input contract.
@interface BGPixelControls : UIView
@property(nonatomic,copy) void (^inputChanged)(CTRSkinInput);
@property(nonatomic,copy) void (^menuPressed)(void);
@property(nonatomic) BOOL previewMode;
@property(nonatomic) NSInteger gameSystem; // 1=3DS (default), 2=DS, 3=GBA
- (ctr::DualScreenLayout)fittedScreens;
@property(nonatomic,readonly) ctr::DualScreenLayout screenSlots;
- (void)releaseTouches;
- (void)refreshLayout;
@end
@implementation BGPixelControls {
 NSMutableArray<NSDictionary*>* _items;NSMutableArray<UIImageView*>* _visuals;
 NSMapTable<UITouch*,NSNumber*>* _touches;ctr::DualScreenLayout _slots;CGSize _lastSize;
}
- (instancetype)init { if((self=[super init])){self.multipleTouchEnabled=YES;self.backgroundColor=UIColor.clearColor;self.opaque=NO;_touches=[NSMapTable strongToStrongObjectsMapTable];_items=[NSMutableArray new];_visuals=[NSMutableArray new];}return self; }
- (ctr::DualScreenLayout)screenSlots { return _slots; }
- (ctr::DualScreenLayout)fittedScreens {
 if(self.gameSystem==2)return {ctr::fit_screen(_slots.top,self.bounds.size.width,self.bounds.size.height,4./3),ctr::fit_screen(_slots.bottom,self.bounds.size.width,self.bounds.size.height,4./3)};
 if(self.gameSystem==3)return {ctr::fit_screen(_slots.top,self.bounds.size.width,self.bounds.size.height,3./2),{0,0,0,0}};
 return ctr::fit_dual_screens(_slots,self.bounds.size.width,self.bounds.size.height);
}
- (void)add:(NSString*)asset kind:(NSString*)kind button:(unsigned)button x:(CGFloat)x y:(CGFloat)y size:(CGFloat)size {
 CGFloat w=self.bounds.size.width,h=self.bounds.size.height;CGRect f=CGRectMake(x*w-size/2,y*h-size/2,size,size);
 if([@[@"start",@"select",@"home",@"l",@"r",@"zl",@"zr"] containsObject:asset]){f.size.width=MAX(48,size*1.5);f.size.height=44;f.origin.x=x*w-f.size.width/2;f.origin.y=y*h-22;}
 UIImageView* image=[[UIImageView alloc] initWithImage:BGAsset([kind isEqual:@"button"]?[@"control-" stringByAppendingString:asset]:asset,0)];image.frame=f;image.contentMode=UIViewContentModeScaleAspectFit;BGNearest(image);[self addSubview:image];[_visuals addObject:image];[_items addObject:@{@"asset":asset,@"kind":kind,@"button":@(button),@"frame":[NSValue valueWithCGRect:f]}];
}
// Reference coordinates describe placement only; all artwork is original BGAsset art.
- (void)place:(NSString*)asset kind:(NSString*)kind button:(unsigned)button rect:(CGRect)r canvas:(CGSize)canvas {
 CGFloat sx=self.bounds.size.width/canvas.width,sy=self.bounds.size.height/canvas.height;
 [self add:asset kind:kind button:button x:CGRectGetMidX(r)/canvas.width y:CGRectGetMidY(r)/canvas.height size:r.size.width*sx];
 CGRect frame=CGRectMake(r.origin.x*sx,r.origin.y*sy,r.size.width*sx,r.size.height*sy);
 NSMutableDictionary* item=[_items.lastObject mutableCopy];item[@"frame"]=[NSValue valueWithCGRect:frame];_items[_items.count-1]=item;
 UIImageView* image=_visuals.lastObject;image.frame=frame;
 if([@[@"l",@"r",@"zl",@"zr"] containsObject:asset])image.contentMode=UIViewContentModeScaleToFill;
}
- (void)refreshLayout {
 [self releaseTouches];for(UIView* v in _visuals)[v removeFromSuperview];[_visuals removeAllObjects];[_items removeAllObjects];CGFloat w=self.bounds.size.width,h=self.bounds.size.height;if(w<=0||h<=0)return;
 BOOL wide=[NSUserDefaults.standardUserDefaults boolForKey:@"BGWide"];
 BOOL compact=h/w>1.6;
 if(self.gameSystem==2||self.gameSystem==3){
  BOOL gba=self.gameSystem==3;
  if(!gba&&compact){
   // Use the 3DS-style stacked display/shoulder arrangement with native DS aspect.
   // The lower display stays usable for touch instead of shrinking between both clusters.
   CGSize canvas=CGSizeMake(375,812);
   _slots={{8./375,35./812,359./375,269.25/812},{58./375,340./812,259./375,194.25/812}};
   [self place:@"dpad" kind:@"dpad" button:0 rect:CGRectMake(8.5,572.5,151,151) canvas:canvas];
   [self place:@"a" kind:@"button" button:8 rect:CGRectMake(313,623,50,50) canvas:canvas];
   [self place:@"b" kind:@"button" button:0 rect:CGRectMake(269,667,50,50) canvas:canvas];
   [self place:@"x" kind:@"button" button:9 rect:CGRectMake(269,579,50,50) canvas:canvas];
   [self place:@"y" kind:@"button" button:1 rect:CGRectMake(225,623,50,50) canvas:canvas];
   [self place:@"l" kind:@"button" button:10 rect:CGRectMake(3,444,42,72) canvas:canvas];
   [self place:@"r" kind:@"button" button:11 rect:CGRectMake(330,444,42,72) canvas:canvas];
   [self place:@"home" kind:@"button" button:99 rect:CGRectMake(20,769,76,32) canvas:canvas];
   [self place:@"select" kind:@"button" button:2 rect:CGRectMake(150,769,76,32) canvas:canvas];
   [self place:@"start" kind:@"button" button:3 rect:CGRectMake(279,769,76,32) canvas:canvas];
   [self setNeedsDisplay];return;
  }
  if(gba)_slots={{.04,.04,.92,.52},{.01,.01,.01,.01}};

  else {
   // Keep the touch screen between the full control clusters, with a clear gutter.
   CGFloat rail=MAX(120,MIN(44,w*.10)*2.5+28);
   CGFloat inset=MAX(.22,rail/w);
   _slots={{.10,.02,.80,.44},{inset,.56,1-2*inset,.35}};
  }
  CGFloat shoulder=gba?.58:.53;

  [self add:@"dpad" kind:@"dpad" button:0 x:gba?.22:(compact?.20:.12) y:.76 size:gba?MIN(140,w*.32):MIN(96,w*.22)];
  CGFloat unit=gba?MIN(78,w*.18):44;
  CGFloat dx=unit*.75/w,dy=unit*.75/h;
  // Reserve 18 points beyond the rightmost button, including its complete frame.
  CGFloat cx=(w-18-unit*.5)/w-dx,cy=.77;
  if(gba){
   [self add:@"a" kind:@"button" button:8 x:.82 y:.73 size:unit];
   [self add:@"b" kind:@"button" button:0 x:.65 y:.82 size:unit];
  }else{
   [self add:@"a" kind:@"button" button:8 x:cx+dx y:cy size:unit];
   [self add:@"b" kind:@"button" button:0 x:cx y:cy+dy size:unit];
   [self add:@"x" kind:@"button" button:9 x:cx y:cy-dy size:unit];
   [self add:@"y" kind:@"button" button:1 x:cx-dx y:cy size:unit];
  }
  [self add:@"l" kind:@"button" button:10 x:.16 y:shoulder size:54];[self add:@"r" kind:@"button" button:11 x:.84 y:shoulder size:54];
  [self add:@"home" kind:@"button" button:99 x:.18 y:.95 size:42];[self add:@"select" kind:@"button" button:2 x:.50 y:.95 size:42];[self add:@"start" kind:@"button" button:3 x:.82 y:.95 size:42];
  [self setNeedsDisplay];return;
 }

 if(compact&&!wide){
  CGSize canvas=CGSizeMake(375,812);
  _slots={{8./375,45./812,359./375,215./812},{44./375,278./812,287./375,215./812}};

  [self place:@"circle" kind:@"circle" button:0 rect:CGRectMake(32,520,104,104) canvas:canvas];
  [self place:@"dpad" kind:@"dpad" button:0 rect:CGRectMake(19,638,130,130) canvas:canvas];
  [self place:@"cstick" kind:@"cstick" button:0 rect:CGRectMake(180,518,56,56) canvas:canvas];
  [self place:@"a" kind:@"button" button:8 rect:CGRectMake(307,594,50,50) canvas:canvas];
  [self place:@"b" kind:@"button" button:0 rect:CGRectMake(257,644,50,50) canvas:canvas];
  [self place:@"x" kind:@"button" button:9 rect:CGRectMake(257,544,50,50) canvas:canvas];
  [self place:@"y" kind:@"button" button:1 rect:CGRectMake(207,594,50,50) canvas:canvas];
  [self place:@"l" kind:@"button" button:10 rect:CGRectMake(2,438,32,56) canvas:canvas];
  [self place:@"zl" kind:@"button" button:12 rect:CGRectMake(2,376,32,56) canvas:canvas];
  [self place:@"r" kind:@"button" button:11 rect:CGRectMake(341,438,32,56) canvas:canvas];
  [self place:@"zr" kind:@"button" button:13 rect:CGRectMake(341,376,32,56) canvas:canvas];
  [self place:@"start" kind:@"button" button:3 rect:CGRectMake(279,779,76,32) canvas:canvas];
  [self place:@"select" kind:@"button" button:2 rect:CGRectMake(150,779,76,32) canvas:canvas];
  [self place:@"home" kind:@"button" button:99 rect:CGRectMake(20,779,76,32) canvas:canvas];
  [self setNeedsDisplay];return;
 }
 CGFloat unit=MAX(44,MIN(60,w*.11));CGFloat ly,dy,cy,by;
 if(wide){_slots={{.10,.04,.42,.60},{.54,.04,.36,.60}};ly=.78;dy=.78;cy=.76;by=.79;}
 else if(compact){_slots={{.03,.03,.94,.26},{.12,.32,.76,.24}};ly=.70;dy=.86;cy=.65;by=.78;}
 else{_slots={{.078,.02,.844,.445},{.29,.595,.42,.292}};ly=.68;dy=.84;cy=.64;by=.77;}
 CGFloat left=wide?.10:.12,right=MIN(wide?.85:.86,(w-unit*1.3)/w);
 [self add:@"circle" kind:@"circle" button:0 x:left y:ly size:wide?unit*1.5:110];[self add:@"dpad" kind:@"dpad" button:0 x:wide?.28:left y:dy size:unit*1.55];
 [self add:@"cstick" kind:@"cstick" button:0 x:wide?.64:.86 y:cy size:64];
 CGFloat dx=unit*.70/w, ddy=unit*.70/h;
 [self add:@"a" kind:@"button" button:8 x:right+dx y:by size:unit];[self add:@"b" kind:@"button" button:0 x:right y:by+ddy size:unit];[self add:@"x" kind:@"button" button:9 x:right y:by-ddy size:unit];[self add:@"y" kind:@"button" button:1 x:right-dx y:by size:unit];
 CGFloat shoulder=wide?.65:(compact?.60:.54);

 [self add:@"l" kind:@"button" button:10 x:.09 y:shoulder size:36];[self add:@"zl" kind:@"button" button:12 x:.26 y:shoulder size:36];[self add:@"zr" kind:@"button" button:13 x:.74 y:shoulder size:36];[self add:@"r" kind:@"button" button:11 x:.91 y:shoulder size:36];
 [self add:@"select" kind:@"button" button:2 x:.31 y:.95 size:42];[self add:@"home" kind:@"button" button:99 x:.50 y:.95 size:42];[self add:@"start" kind:@"button" button:3 x:.69 y:.95 size:42];
 [self setNeedsDisplay];
}
- (void)layoutSubviews { [super layoutSubviews];if(!CGSizeEqualToSize(_lastSize,self.bounds.size)){_lastSize=self.bounds.size;[self refreshLayout];} }
- (void)drawRect:(CGRect)rect {
 (void)rect;CGContextRef c=UIGraphicsGetCurrentContext();CGContextSetInterpolationQuality(c,kCGInterpolationNone);
 // Controls float over the shared grid; only screen bezels receive a backing.
 auto fit=[self fittedScreens];
 UIImage* bezel=[BGAsset(@"skin-bezel",0) resizableImageWithCapInsets:UIEdgeInsetsMake(6,6,6,6) resizingMode:UIImageResizingModeStretch];
 for(auto r:{fit.top,fit.bottom}){
  if(r.width<=0||r.height<=0)continue;
  CGRect screen=CGRectMake(r.x,r.y,r.width,r.height);
  // The shell must never cover the live lower touchscreen.
  CGContextClearRect(c,screen);
  if(self.previewMode){[BGColor(@"080C12") setFill];CGContextFillRect(c,screen);NSString* text=self.gameSystem==3?@"GAME SCREEN":((r.x==fit.top.x&&r.y==fit.top.y)?@"TOP SCREEN":@"TOUCH SCREEN");[text drawAtPoint:CGPointMake(r.x+12,r.y+12) withAttributes:@{NSFontAttributeName:[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightMedium],NSForegroundColorAttributeName:BGColor(@"9FAFB9")}];}
  [bezel drawInRect:CGRectInset(screen,-6,-6)];
 }
 if(self.gameSystem<2 && self.bounds.size.height/self.bounds.size.width>1.6 && ![NSUserDefaults.standardUserDefaults boolForKey:@"BGWide"]){
  [BGAsset(@"skin-phone-details",0) drawInRect:self.bounds];
 }
}
// Keep artwork geometry separate from forgiving shoulder touch targets.
- (CGRect)touchFrameForItem:(NSDictionary*)item {
 CGRect frame=[item[@"frame"] CGRectValue];unsigned button=[item[@"button"] unsignedIntValue];
 if([item[@"kind"] isEqual:@"button"]&&button>=10&&button<=13)
  return CGRectIntersection(self.bounds,CGRectInset(frame,-12,-10));
 return frame;
}
- (NSInteger)controlAtPoint:(CGPoint)point {
 // Visible controls always win over another control's expanded margin.
 for(NSUInteger i=0;i<_items.count;++i)
  if(CGRectContainsPoint([_items[i][@"frame"] CGRectValue],point))return (NSInteger)i;
 auto fit=[self fittedScreens];if(ctr::map_bottom_touch(fit,point.x,point.y))return -1;
 NSInteger nearest=-2;CGFloat best=CGFLOAT_MAX;
 for(NSUInteger i=0;i<_items.count;++i){
  NSDictionary* item=_items[i];if(!CGRectContainsPoint([self touchFrameForItem:item],point))continue;
  CGRect frame=[item[@"frame"] CGRectValue];CGFloat dx=point.x-CGRectGetMidX(frame),dy=point.y-CGRectGetMidY(frame);
  CGFloat distance=dx*dx+dy*dy;if(distance<best){best=distance;nearest=(NSInteger)i;}
 }
 return nearest;
}
- (void)emit {
 CTRSkinInput input;for(NSUInteger i=0;i<_items.count;++i){NSDictionary* item=_items[i];NSString* kind=item[@"kind"];CGRect r=[item[@"frame"] CGRectValue];BOOL pressed=NO;CGPoint offset=CGPointZero;
  for(UITouch* touch in _touches){if([_touches objectForKey:touch].integerValue!=(NSInteger)i)continue;CGPoint p=[touch locationInView:self];
   if([kind isEqual:@"circle"]||[kind isEqual:@"cstick"]){double x=(p.x-CGRectGetMidX(r))/(r.size.width/2),y=(p.y-CGRectGetMidY(r))/(r.size.height/2);double len=hypot(x,y);if(len>1){x/=len;y/=len;}if(len<.12)x=y=0;offset=CGPointMake(x*12,y*12);if([kind isEqual:@"circle"]){input.lx=x*32767;input.ly=y*32767;}else{input.rx=x*32767;input.ry=y*32767;}pressed=YES;
   }else if(CGRectContainsPoint(CGRectInset([self touchFrameForItem:item],-4,-4),p)){pressed=YES;if([kind isEqual:@"dpad"]){double x=(p.x-CGRectGetMidX(r))/(r.size.width/2),y=(p.y-CGRectGetMidY(r))/(r.size.height/2);if(x<-.3)input.buttons|=1u<<6;if(x>.3)input.buttons|=1u<<7;if(y<-.3)input.buttons|=1u<<4;if(y>.3)input.buttons|=1u<<5;}else{unsigned b=[item[@"button"] unsignedIntValue];if(b<16)input.buttons|=1u<<b;}}
  }
  NSInteger frame=pressed?1:0;if([kind isEqual:@"dpad"]){BOOL u=input.buttons&(1u<<4),d=input.buttons&(1u<<5),l=input.buttons&(1u<<6),rr=input.buttons&(1u<<7);frame=u?(l?5:rr?6:1):d?(l?7:rr?8:2):l?3:rr?4:0;}
  NSString* asset=[kind isEqual:@"button"]?[@"control-" stringByAppendingString:item[@"asset"]]:item[@"asset"];_visuals[i].image=BGAsset(asset,frame);_visuals[i].transform=CGAffineTransformMakeTranslation(offset.x,offset.y);
 }
 auto fit=[self fittedScreens];
 for(UITouch* t in _touches)if([_touches objectForKey:t].integerValue==-1){CGPoint p=[t locationInView:self];auto mapped=ctr::map_bottom_touch(fit,p.x,p.y);if(mapped){input.tx=mapped->x;input.ty=mapped->y;input.touching=true;}}
 if(self.inputChanged)self.inputChanged(input);
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
 (void)event;for(UITouch* t in touches){NSInteger target=[self controlAtPoint:[t locationInView:self]];if(target>=-1)[_touches setObject:@(target) forKey:t];}[self emit];
}
- (void)touchesMoved:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e { (void)t;(void)e;[self emit]; }
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { (void)event;BOOL menu=NO;for(UITouch* t in touches){NSNumber* n=[_touches objectForKey:t];if(n&&n.integerValue>=0){NSDictionary* item=_items[n.unsignedIntegerValue];if([item[@"button"] integerValue]==99&&CGRectContainsPoint([item[@"frame"] CGRectValue],[t locationInView:self]))menu=YES;}[_touches removeObjectForKey:t];}[self emit];if(menu&&self.menuPressed)self.menuPressed(); }
- (void)touchesCancelled:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e { (void)e;for(UITouch* touch in t)[_touches removeObjectForKey:touch];[self emit]; }
- (void)releaseTouches { [_touches removeAllObjects];if(self.bounds.size.width>0&&self.bounds.size.height>0&&ctr::valid_slot(_slots.top))[self emit];else if(self.inputChanged)self.inputChanged(CTRSkinInput{}); }
@end
