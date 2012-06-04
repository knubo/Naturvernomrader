//
//  no_knuboFirstViewController.m
//  Naturvernområder
//
//  Created by Knut Erik Borgen on 31.05.12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#import "no_knuboFirstViewController.h"
#import "Shapefile.h"
@interface no_knuboFirstViewController ()

@end

@implementation no_knuboFirstViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view, typically from a nib.
    
    
    NSString *shapePath = [[NSBundle mainBundle] pathForResource:@"naturvern_simple" ofType:@"shp"];	
    
    Shapefile *shapefile = [[Shapefile alloc] init];
	
	//[myView setNeedsDisplay:YES];
	BOOL bLoad = [shapefile loadShapefile:shapePath];
	
	if(bLoad)
	{
//		long nShapefileType = [shapefile shapefileType];
		
        //		if(nShapefileType == kShapeTypePoint)
        //			[self.mapView addAnnotations:shapefile.objects];
        
        NSLog( @"Count: %d", [[shapefile objects] count]);
        
    }

}

- (void)viewDidUnload
{
    [super viewDidUnload];
    // Release any retained subviews of the main view.
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)interfaceOrientation
{
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPhone) {
        return (interfaceOrientation != UIInterfaceOrientationPortraitUpsideDown);
    } else {
        return YES;
    }
}

@end
