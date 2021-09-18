#import "MASPreferencesWindowController.h"
#import "StatusManager.h"
#import "BackupPreferencesViewController.h"
#import "StatusPreferencesViewController.h"

NSString *const kMASPreferencesWindowControllerDidChangeViewNotification = @"MASPreferencesWindowControllerDidChangeViewNotification";

static NSString *const kMASPreferencesFrameTopLeftKey = @"MASPreferences Frame Top Left";
static NSString *const kMASPreferencesSelectedViewKey = @"MASPreferences Selected Identifier View";

static NSString *const PreferencesKeyForViewBounds (NSString *identifier)
{
    return [NSString stringWithFormat:@"MASPreferences %@ Frame", identifier];
}

@interface MASPreferencesWindowController () // Private

- (NSViewController <MASPreferencesViewController> *)viewControllerForIdentifier:(NSString *)identifier;
- (NSViewController <MASPreferencesViewController> *)firstViewController;

@property (readonly) NSArray *toolbarItemIdentifiers;
@property (nonatomic, retain) NSViewController <MASPreferencesViewController> *selectedViewController;

@end

#pragma mark -

@implementation MASPreferencesWindowController

@synthesize viewControllers = _viewControllers;
@synthesize selectedViewController = _selectedViewController;
@synthesize title = _title;
@synthesize aToolbar = _aToolbar;

#pragma mark -

- (id)initWithViewControllers:(NSArray *)viewControllers
{
    return [self initWithViewControllers:viewControllers title:nil];
}

- (id)initWithViewControllers:(NSArray *)viewControllers title:(NSString *)title
{
    if ((self = [super initWithWindowNibName:@"MASPreferencesWindow"]))
    {
#ifdef __has_feature

#if __has_feature(objc_arc)
        _viewControllers = viewControllers;
#else
        _viewControllers = [viewControllers retain];
#endif

#else
			  _viewControllers = [viewControllers retain];
#endif
        _minimumViewRects = [[NSMutableDictionary alloc] init];
        _title = [title copy];
        
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(enableTabs:) name:@"EnableTabs" object:nil];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self name:@"EnableTabs" object:nil];
    [self setSelectedViewController:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [[self window] setDelegate:nil];
#ifdef __has_feature
#if !__has_feature(objc_arc)
    [_aToolbar release];
    [_viewControllers release];
    [_selectedViewController release];
    [_minimumViewRects release];
    [_title release];
    [super dealloc];
#endif
#else
    [_aToolbar release];
	  [_viewControllers release];
	  [_selectedViewController release];
	  [_minimumViewRects release];
	  [_title release];
	  [super dealloc];	
#endif
}

#pragma mark -

- (void)windowDidLoad
{
    if ([self.title length] > 0)
        [[self window] setTitle:self.title];

    if ([self.viewControllers count])
        self.selectedViewController = [self viewControllerForIdentifier:[[NSUserDefaults standardUserDefaults] stringForKey:kMASPreferencesSelectedViewKey]] ?: [self firstViewController];

    NSString *origin = [[NSUserDefaults standardUserDefaults] stringForKey:kMASPreferencesFrameTopLeftKey];
    if (origin)
        [self.window setFrameTopLeftPoint:NSPointFromString(origin)];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowDidMove:)   name:NSWindowDidMoveNotification object:self.window];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowDidResize:) name:NSWindowDidResizeNotification object:self.window];
}

- (NSViewController <MASPreferencesViewController> *)firstViewController {
    for (id viewController in self.viewControllers)
        if ([viewController isKindOfClass:[NSViewController class]])
            return viewController;

    return nil;
}

#pragma mark -
#pragma mark NSWindowDelegate

- (BOOL)windowShouldClose:(id)sender
{
    return !self.selectedViewController || [self.selectedViewController commitEditing];
}

- (void)windowDidMove:(NSNotification*)aNotification
{
    [[NSUserDefaults standardUserDefaults] setObject:NSStringFromPoint(NSMakePoint(NSMinX([self.window frame]), NSMaxY([self.window frame]))) forKey:kMASPreferencesFrameTopLeftKey];
}

- (void)windowDidResize:(NSNotification*)aNotification
{
    NSViewController <MASPreferencesViewController> *viewController = self.selectedViewController;
    if (viewController)
        [[NSUserDefaults standardUserDefaults] setObject:NSStringFromRect([viewController.view bounds]) forKey:PreferencesKeyForViewBounds(viewController.identifier)];
}

#pragma mark -
#pragma mark Accessors

- (NSArray *)toolbarItemIdentifiers
{
    NSMutableArray *identifiers = [NSMutableArray arrayWithCapacity:_viewControllers.count];
    for (id viewController in _viewControllers)
        if (viewController == [NSNull null])
            [identifiers addObject:NSToolbarFlexibleSpaceItemIdentifier];
        else
            [identifiers addObject:[viewController identifier]];
    return identifiers;
}

#pragma mark -

- (NSUInteger)indexOfSelectedController
{
    NSUInteger index = [self.toolbarItemIdentifiers indexOfObject:self.selectedViewController.identifier];
    return index;
}

#pragma mark -
#pragma mark NSToolbarDelegate

- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar
{
    NSArray *identifiers = self.toolbarItemIdentifiers;
    return identifiers;
}                   
                   
- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar
{
    NSArray *identifiers = self.toolbarItemIdentifiers;
    return identifiers;
}

- (NSArray *)toolbarSelectableItemIdentifiers:(NSToolbar *)toolbar
{
    NSArray *identifiers = self.toolbarItemIdentifiers;
    return identifiers;
}

- (NSToolbarItem *)toolbar:(CustomToolbar *)toolbar itemForItemIdentifier:(NSString *)itemIdentifier willBeInsertedIntoToolbar:(BOOL)flag
{
    NSToolbarItem *toolbarItem = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];;
    NSArray *identifiers = self.toolbarItemIdentifiers;
    NSUInteger controllerIndex = [identifiers indexOfObject:itemIdentifier];
    if (controllerIndex != NSNotFound)
    {
        id <MASPreferencesViewController> controller = [_viewControllers objectAtIndex:controllerIndex];
        toolbarItem.image = controller.toolbarItemImage;
        toolbarItem.label = controller.toolbarItemLabel;
        toolbarItem.target = self;
        if (!toolbar.allTabsEnabled) {
            if (![controller isKindOfClass:[StatusPreferencesViewController class]] && ![controller isKindOfClass:[BackupPreferencesViewController class]]) {
                toolbarItem.action = @selector(toolbarItemDidClick:);
            }
        }else {
            toolbarItem.action = @selector(toolbarItemDidClick:);
        }
    }
    
#ifdef __has_feature
#if !__has_feature(objc_arc)
    [toolbarItem autorelease];
#endif
#else
	  [toolbarItem autorelease];
#endif
    return toolbarItem;
}

#pragma mark -
#pragma mark Private methods

- (void)clearResponderChain
{
    // Remove view controller from the responder chain
    NSResponder *chainedController = self.window.nextResponder;
    if ([self.viewControllers indexOfObject:chainedController] == NSNotFound)
        return;
    self.window.nextResponder = chainedController.nextResponder;
    chainedController.nextResponder = nil;
}

- (void)patchResponderChain
{
    [self clearResponderChain];
    
    NSViewController *selectedController = self.selectedViewController;
    if (!selectedController)
        return;
    
    // Add current controller to the responder chain
    NSResponder *nextResponder = self.window.nextResponder;
    self.window.nextResponder = selectedController;
    selectedController.nextResponder = nextResponder;
}

- (NSViewController <MASPreferencesViewController> *)viewControllerForIdentifier:(NSString *)identifier
{
    for (id viewController in self.viewControllers) {
        if (viewController == [NSNull null]) continue;
        if ([[viewController identifier] isEqualToString:identifier])
            return viewController;
    }
    return nil;
}

#pragma mark -

- (void)setSelectedViewController:(NSViewController <MASPreferencesViewController> *)controller
{
    if (_selectedViewController == controller)
        return;

    if (_selectedViewController)
    {
        // Check if we can commit changes for old controller
        if (![_selectedViewController commitEditing])
        {
            [[self.window toolbar] setSelectedItemIdentifier:_selectedViewController.identifier];
            return;
        }

#ifdef __has_feature

#if __has_feature(objc_arc)
        [self.window setContentView:[[NSView alloc] init]];
#else
        [self.window setContentView:[[[NSView alloc] init] autorelease]];
#endif

#else
			[self.window setContentView:[[[NSView alloc] init] autorelease]];
#endif

        if ([_selectedViewController respondsToSelector:@selector(viewDidDisappear)])
            [_selectedViewController viewDidDisappear];

#ifdef __has_feature
#if !__has_feature(objc_arc)
        [_selectedViewController release];
#endif
#else
			[_selectedViewController release];
#endif
        _selectedViewController = nil;
    }

    if (!controller)
        return;

    // Retrieve the new window tile from the controller view
    if ([self.title length] == 0)
    {
        NSString *label = controller.toolbarItemLabel;
        self.window.title = label;
    }
    
    [[self.window standardWindowButton:NSWindowZoomButton] setEnabled:NO];
    if ([controller isKindOfClass:[BackupPreferencesViewController class]]) [[self.window standardWindowButton:NSWindowZoomButton] setEnabled:YES];
    
    [[self.window toolbar] setSelectedItemIdentifier:controller.identifier];

    // Record new selected controller in user defaults
    [[NSUserDefaults standardUserDefaults] setObject:controller.identifier forKey:kMASPreferencesSelectedViewKey];
    
    NSView *controllerView = controller.view;

    // Retrieve current and minimum frame size for the view
    NSString *oldViewRectString = [[NSUserDefaults standardUserDefaults] stringForKey:PreferencesKeyForViewBounds(controller.identifier)];
    NSString *minViewRectString = [_minimumViewRects objectForKey:controller.identifier];
    if (!minViewRectString)
        [_minimumViewRects setObject:NSStringFromRect(controllerView.bounds) forKey:controller.identifier];
    
    BOOL sizableWidth = ([controller respondsToSelector:@selector(hasResizableWidth)]
                         ? controller.hasResizableWidth
                         : controllerView.autoresizingMask & NSViewWidthSizable);
    BOOL sizableHeight = ([controller respondsToSelector:@selector(hasResizableHeight)]
                          ? controller.hasResizableHeight
                          : controllerView.autoresizingMask & NSViewHeightSizable);
    
    NSRect oldViewRect = oldViewRectString ? NSRectFromString(oldViewRectString) : controllerView.bounds;
    NSRect minViewRect = minViewRectString ? NSRectFromString(minViewRectString) : controllerView.bounds;
    oldViewRect.size.width  = NSWidth(oldViewRect)  < NSWidth(minViewRect)  || !sizableWidth  ? NSWidth(minViewRect)  : NSWidth(oldViewRect);
    oldViewRect.size.height = NSHeight(oldViewRect) < NSHeight(minViewRect) || !sizableHeight ? NSHeight(minViewRect) : NSHeight(oldViewRect);

    [controllerView setFrame:oldViewRect];

    // Calculate new window size and position
    NSRect oldFrame = [self.window frame];
    NSRect newFrame = [self.window frameRectForContentRect:oldViewRect];
    newFrame = NSOffsetRect(newFrame, NSMinX(oldFrame), NSMaxY(oldFrame) - NSMaxY(newFrame));

    // Setup min/max sizes and show/hide resize indicator
    [self.window setContentMinSize:minViewRect.size];
    [self.window setContentMaxSize:NSMakeSize(sizableWidth ? CGFLOAT_MAX : NSWidth(oldViewRect), sizableHeight ? CGFLOAT_MAX : NSHeight(oldViewRect))];
    [self.window setShowsResizeIndicator:sizableWidth || sizableHeight];

    [self.window setFrame:newFrame display:YES animate:[self.window isVisible]];

#ifdef __has_feature
#if __has_feature(objc_arc)
    _selectedViewController = controller;
#else
    _selectedViewController = [controller retain];
#endif
	
#else
	_selectedViewController = [controller retain];
#endif

    if ([controller respondsToSelector:@selector(viewWillAppear)])
        [controller viewWillAppear];
    
    [self.window setContentView:controllerView];
    [self.window recalculateKeyViewLoop];
    if ([self.window firstResponder] == self.window) {
        if ([controller respondsToSelector:@selector(initialKeyView)])
            [self.window makeFirstResponder:[controller initialKeyView]];
        else
            [self.window selectKeyViewFollowingView:controllerView];
    }
    
    // Insert view controller into responder chain
    [self patchResponderChain];

    [[NSNotificationCenter defaultCenter] postNotificationName:kMASPreferencesWindowControllerDidChangeViewNotification object:self];
}

- (void)toolbarItemDidClick:(id)sender
{
    if ([sender respondsToSelector:@selector(itemIdentifier)])
        self.selectedViewController = [self viewControllerForIdentifier:[sender itemIdentifier]];
}

#pragma mark -
#pragma mark Public methods

- (void)selectControllerAtIndex:(NSUInteger)controllerIndex
{
    if (NSLocationInRange(controllerIndex, NSMakeRange(0, _viewControllers.count)))
        self.selectedViewController = [self.viewControllers objectAtIndex:controllerIndex];
}

#pragma mark -
#pragma mark Actions

- (IBAction)goNextTab:(id)sender
{
    NSUInteger selectedIndex = self.indexOfSelectedController;
    NSUInteger numberOfControllers = [_viewControllers count];

    do { selectedIndex = (selectedIndex + 1) % numberOfControllers; }
    while ([_viewControllers objectAtIndex:selectedIndex] == [NSNull null]);

    [self selectControllerAtIndex:selectedIndex];
}

- (IBAction)goPreviousTab:(id)sender
{
    NSUInteger selectedIndex = self.indexOfSelectedController;
    NSUInteger numberOfControllers = [_viewControllers count];

    do { selectedIndex = (selectedIndex + numberOfControllers - 1) % numberOfControllers; }
    while ([_viewControllers objectAtIndex:selectedIndex] == [NSNull null]);

    [self selectControllerAtIndex:selectedIndex];
}

- (void) enableTabs:(NSNotification* )notofication
{
    if (![notofication object]) return;
    BOOL shouldEnable = [[notofication object] boolValue];
    [_aToolbar setAllTabsEnabled:shouldEnable];
    [self validateItems];
    [_aToolbar validateVisibleItems];
}

- (void)validateItems
{
    for (NSToolbarItem* item in _aToolbar.visibleItems) {
        if (_aToolbar.allTabsEnabled) {
            item.action = @selector(toolbarItemDidClick:);
        }else {
            if (item ) {
                NSArray *identifiers = self.toolbarItemIdentifiers;
                NSUInteger controllerIndex = [identifiers indexOfObject:item.itemIdentifier];
                id <MASPreferencesViewController> controller = [_viewControllers objectAtIndex:controllerIndex];
                if (![controller isKindOfClass:[StatusPreferencesViewController class]] && ![controller isKindOfClass:[BackupPreferencesViewController class]]) {
                    item.action = @selector(toolbarItemDidClick:);
                }else {
                    item.action = nil;
                }
            }
        }
    }
}


@end

@implementation CustomToolbar

@synthesize allTabsEnabled = _allTabsEnabled;

@end
