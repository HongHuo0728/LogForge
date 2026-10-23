"""Real Duo simulator smoke tests. No injected layout or simulated processing states.

Appium controls the simulator hinge, orientation and capture display separately.
An unsupported/unapplied pose is a failing test, never silently marked as tested.
"""
import base64
import json
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build/logs/duo'
BUNDLE = 'Shizhihe.LogForge-For-iPhone'
ELEMENT = 'element-6066-11e4-a52e-4f735466cecf'


def until(operation, timeout=45):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            result = operation()
            if result:
                return result
        except (AssertionError, KeyError, RuntimeError, urllib.error.URLError) as error:
            last = error
        time.sleep(0.5)
    raise AssertionError(f'Condition not met after {timeout}s: {last}')


class Driver:
    def __init__(self):
        self.session = ''

    def request(self, method, route, data=None, timeout=180):
        payload = None if data is None else json.dumps(data).encode()
        request = urllib.request.Request('http://127.0.0.1:4723' + route,
            data=payload, headers={'Content-Type':'application/json'}, method=method)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                result = json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError(error.read().decode(errors='replace')[:2000]) from error
        value = result.get('value')
        if isinstance(value, dict) and value.get('error'):
            raise RuntimeError(value)
        return value

    def call(self, method, route, data=None):
        return self.request(method, '/session/' + self.session + route, data)

    def mobile(self, command, **arguments):
        return self.call('POST', '/execute/sync', {'script':'mobile: ' + command, 'args':[arguments]})

    def element(self, identifier):
        return self.call('POST', '/element', {'using':'accessibility id', 'value':identifier})[ELEMENT]

    def text(self, identifier):
        return self.call('GET', '/element/' + self.element(identifier) + '/attribute/label')

    def tap(self, identifier):
        try:
            element = self.element(identifier)
            if not self.call('GET', '/element/' + element + '/attribute/visible') in ('true', True):
                raise RuntimeError('Element is not visible')
        except RuntimeError:
            more = self.call('POST', '/elements', {'using':'-ios predicate string',
                'value':"label == 'More' OR label == 'More Actions' OR name == 'ToolbarOverflowMenu'"})
            assert more, f'{identifier} and system overflow are unavailable'
            self.call('POST', '/element/' + more[0][ELEMENT] + '/click', {})
            element = self.element(identifier)
        self.call('POST', '/element/' + element + '/click', {})

    def scroll_to(self, identifier):
        for _ in range(12):
            try:
                element = self.element(identifier)
                if self.call('GET', '/element/' + element + '/attribute/visible') in ('true', True):
                    return element
            except RuntimeError:
                pass
            self.mobile('swipe', direction='up')
        raise AssertionError('Cannot scroll to ' + identifier)

    def launch(self, arguments, language='en'):
        self.mobile('terminateApp', bundleId=BUNDLE)
        self.mobile('launchApp', bundleId=BUNDLE, arguments=['-AppleLanguages',f'({language})',
            '-AppleLocale','en_US','--ui-testing',*arguments])
        return until(self.state)

    def state(self):
        return json.loads(self.text('workspaceState'))

    def states(self):
        elements = self.call('POST', '/elements', {'using':'accessibility id', 'value':'workspaceState'})
        return [json.loads(self.call('GET', '/element/'+e[ELEMENT]+'/attribute/label')) for e in elements
                if self.call('GET','/element/'+e[ELEMENT]+'/attribute/visible') in ('true',True)]

    def capture(self, name, expected_display=None):
        png = base64.b64decode(self.call('GET', '/screenshot'))
        assert png.startswith(b'\x89PNG\r\n\x1a\n')
        width, height = struct.unpack('>II', png[16:24])
        if expected_display:
            dimensions = expected_display['metrics']
            assert sorted((width, height)) == sorted((dimensions['widthPixels'], dimensions['heightPixels']))
        (OUTPUT / (name + '.png')).write_bytes(png)
        xml = self.call('GET', '/source')
        (OUTPUT / (name + '.xml')).write_text(xml, encoding='utf-8')
        # Native screenshots are evidence of the selected screen, not merely a requested angle.
        return {'screenshot':name+'.png', 'pixels':[width,height]}

    def window(self):
        source = ET.fromstring(self.call('GET', '/source'))
        windows = [w for w in source.iter('XCUIElementTypeWindow') if w.get('visible') != 'false']
        window = max(windows, key=lambda w:float(w.get('width',0))*float(w.get('height',0)))
        return {key:float(window.get(key,0)) for key in ('x','y','width','height')}

    def drag_home(self, destination_x, destination_y):
        window = self.window()
        # Apple's gesture pauses in the centre before continuing to an edge.
        x, y = window['x']+window['width']/2, window['y']+window['height']/2
        self.call('POST', '/actions', {'actions':[{'type':'pointer', 'id':'home-gesture',
            'parameters':{'pointerType':'touch'}, 'actions':[
                {'type':'pointerMove','duration':0,'x':round(x),'y':round(window['y']+window['height']-3)},
                {'type':'pointerDown','button':0},
                {'type':'pointerMove','duration':650,'x':round(x),'y':round(y)},
                {'type':'pause','duration':600},
                {'type':'pointerMove','duration':650,'x':round(destination_x),'y':round(destination_y)},
                {'type':'pause','duration':250}, {'type':'pointerUp','button':0}]}]})

    def pose(self, angle, orientation, screen, layout, divided=False):
        self.mobile('setSimulatedHingeAngle', angle=angle)
        self.call('POST', '/orientation', {'orientation':orientation})
        until(lambda: abs(float(self.mobile('getSimulatedHingeAngle')) - angle) < 1)
        displays = self.mobile('listDisplays')
        assert len(displays) >= 2, displays
        displays = sorted(displays, key=lambda d:d['metrics']['widthPixels'] * d['metrics']['heightPixels'])
        display = displays[0] if screen == 'outer' else displays[-1]
        self.call('POST', '/appium/settings', {'settings':{'currentDisplayId':display['id']}})
        def geometry():
            values = self.text('workspaceGeometry').split('|') if layout else []
            if layout:
                assert values[0] == layout, values
                assert int(values[3]) > 0 if divided else int(values[3]) == 0, values
            # Verify the app's real window occupies the selected display. Selecting a
            # screenshot display alone does NOT move the application onto that display.
            source = ET.fromstring(self.call('GET', '/source'))
            windows = [w for w in source.iter('XCUIElementTypeWindow') if w.get('visible') != 'false']
            points = sorted((display['metrics']['widthPixels']/display['metrics']['density'],
                             display['metrics']['heightPixels']/display['metrics']['density']))
            assert any(all(abs(a-b) < 3 for a,b in zip(sorted((float(w.get('width',0)),
                       float(w.get('height',0)))), points)) for w in windows), 'App window is on the wrong display'
            return values or ['presented']
        values = until(geometry)
        return display, values


def test_split_view(driver, report):
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    baseline = driver.state()
    full = driver.window()
    result = {'passed':False, 'fullWindow':full, 'syntheticLayout':False}
    report['splitView'] = result
    # Apple documents dragging the home indicator to either edge to enter Split View.
    driver.drag_home(full['x']+8, full['y']+full['height']/2)
    def split(side):
        window = driver.window()
        assert window['width'] < full['width']*0.75, window
        assert (window['x'] < full['width']/3) == (side == 'left'), window
        return window
    result['leftWindow'] = until(lambda: split('left'))
    driver.mobile('activateApp', bundleId='com.apple.mobilesafari')
    driver.call('POST', '/appium/settings', {'settings':{'defaultActiveApplication':BUNDLE}})
    until(lambda: driver.state()['ids'] == baseline['ids'])
    assert driver.text('workspaceGeometry').split('|')[0] == 'single'
    driver.capture('split-view-left')
    driver.tap('conversionSettings')
    driver.tap('option-exposure'); driver.tap('option-exposure-choice-1')
    driver.drag_home(full['x']+full['width']-8, full['y']+full['height']/2)
    result['rightWindow'] = until(lambda: split('right'))
    assert '1 EV' in driver.text('option-exposure'), 'Split View move lost draft'
    driver.capture('split-view-right-draft')
    driver.tap('cancelSettings')
    assert driver.state()['ids'] == baseline['ids']
    assert driver.state()['exposure'] == baseline['exposure']
    driver.capture('split-view-right')
    # Move the divider to the far left to expand the right-hand LogForge app.
    driver.mobile('dragFromToForDuration',duration=0.5,
        fromX=full['x']+full['width']/2,fromY=full['y']+full['height']/2,
        toX=full['x']+2,toY=full['y']+full['height']/2)
    until(lambda: abs(driver.window()['width']-full['width']) < 3)
    assert driver.state()['ids'] == baseline['ids']
    driver.capture('split-view-restored')
    result['passed'] = True
    report['checks'].append('real left/right Split View with Safari, settings draft and full-screen restoration')


def test_system_keyboard(driver, report):
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    baseline = driver.state()
    driver.tap('importVideo'); driver.tap('Choose Files')
    searches = until(lambda: driver.call('POST', '/elements',
        {'using':'class name','value':'XCUIElementTypeSearchField'}))
    search = searches[0][ELEMENT]
    driver.call('POST', '/element/'+search+'/click', {})
    entered = 'LogForge keyboard layout'
    driver.call('POST', '/element/'+search+'/value', {'text':entered})
    until(lambda: driver.call('POST','/elements',{'using':'class name','value':'XCUIElementTypeKeyboard'}))
    driver.capture('full-system-keyboard')
    # The system keyboard owns its split/full toggle. Do not implement a keyboard
    # extension or infer splitting merely because a keyboard exists after folding.
    driver.mobile('setSimulatedHingeAngle', angle=120)
    until(lambda: abs(float(driver.mobile('getSimulatedHingeAngle'))-120) < 1)
    driver.capture('book-system-keyboard')
    keyboard = driver.call('POST','/elements',{'using':'class name','value':'XCUIElementTypeKeyboard'})[0][ELEMENT]
    more = driver.call('POST', '/element/'+keyboard+'/elements', {'using':'-ios predicate string',
        'value':"label == 'More' OR label == 'More Options' OR name == 'More'"})
    if more:
        driver.call('POST','/element/'+more[0][ELEMENT]+'/click',{})
    controls = driver.call('POST','/elements',{'using':'-ios predicate string',
        'value':"label == 'Split' OR label == 'Split Keyboard'"})
    if controls:
        driver.call('POST','/element/'+controls[0][ELEMENT]+'/click',{})

    def split_geometry():
        tree = ET.fromstring(driver.call('GET','/source'))
        keyboard = next(tree.iter('XCUIElementTypeKeyboard'))
        row = []
        for letter in 'qwertyuiop':
            keys = [k for k in keyboard.iter() if k.tag in ('XCUIElementTypeKey','XCUIElementTypeButton')
                    and k.get('visible') != 'false' and (k.get('label') or k.get('name') or '').lower() == letter]
            assert keys, 'Missing native keyboard key: '+letter
            row.append({key:float(keys[0].get(key,0)) for key in ('x','y','width','height')})
        gaps = [b['x']-(a['x']+a['width']) for a,b in zip(row,row[1:])]
        # QWERTY's T/Y boundary must have a real gap exceeding ordinary key spacing.
        assert gaps[4] > max(16, statistics.median(gaps)+16, statistics.median(k['width'] for k in row)*0.45), gaps
        assert max(k['y'] for k in row)-min(k['y'] for k in row) < 10, row
        return {'keys':row, 'gaps':gaps}

    result = {'passed':False, 'systemInput':True, 'splitControlExposed':bool(controls)}
    report['keyboard'] = result
    result['splitGeometry'] = until(split_geometry)
    result['splitVerified'] = True
    driver.capture('book-split-keyboard')
    # Type while actually split and confirm the native text field receives input.
    driver.call('POST', '/element/'+search+'/value', {'text':' split'})
    entered += ' split'
    until(lambda: driver.call('GET','/element/'+search+'/attribute/value') == entered)
    # Keep the document picker and its real system keyboard alive while rotating/folding.
    driver.mobile('setSimulatedHingeAngle', angle=90)
    driver.call('POST', '/orientation', {'orientation':'PORTRAIT'})
    until(lambda: abs(float(driver.mobile('getSimulatedHingeAngle'))-90) < 1)
    keyboards = until(lambda: driver.call('POST','/elements',
        {'using':'class name','value':'XCUIElementTypeKeyboard'}))
    assert keyboards, 'Keyboard disappeared during fold'
    driver.capture('tabletop-system-keyboard')
    searches = driver.call('POST','/elements',{'using':'class name','value':'XCUIElementTypeSearchField'})
    assert any(driver.call('GET','/element/'+s[ELEMENT]+'/attribute/value') == entered for s in searches)
    result['foldContinuity'] = True
    # Dismiss keyboard/search first if the picker exposes two Cancel actions.
    for _ in range(3):
        try:
            driver.tap('Cancel')
        except RuntimeError:
            break
        try:
            if driver.state()['ids'] == baseline['ids']:
                break
        except RuntimeError:
            pass
    assert until(driver.state)['ids'] == baseline['ids']
    result['passed'] = True
    report['checks'].append('actual Files search keyboard: split key geometry, input, book/tabletop continuity and dismissal')


def test_multi_window(driver, report):
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    baseline = driver.state()
    report['multiWindow'] = {'passed':False, 'syntheticLayout':False}
    assert baseline['supportsMultipleWindows'], 'System did not enable multiple windows on the inner display'
    driver.tap('newWorkspace')
    driver.tap('newWorkspaceAction')
    def two_scenes():
        states = driver.states()
        assert len({s['scene'] for s in states}) >= 2, 'Second native scene did not appear'
        assert all(s['ids'] == baseline['ids'] and s['queueInstance'] == baseline['queueInstance'] for s in states)
        return states
    states = until(two_scenes)
    driver.capture('two-logforge-windows')
    driver.pose(0, 'PORTRAIT', 'outer', 'single')
    assert driver.state()['ids'] == baseline['ids']
    assert driver.state()['queueInstance'] == baseline['queueInstance']
    driver.capture('multi-window-to-outer')
    report['multiWindow'].update(passed=True, scenes=[s['scene'] for s in states])
    report['checks'].append('two real LogForge scenes share the queue and preserve it on the outer display')


def test_file_drop(driver, device, report):
    driver.launch([])
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    baseline = driver.state()
    assert not baseline['ids']
    full = driver.window()
    driver.drag_home(full['x']+8, full['y']+full['height']/2)
    driver.call('POST','/appium/settings',{'settings':{'defaultActiveApplication':BUNDLE}})
    until(lambda: driver.window()['width'] < full['width']*0.75)
    destination = driver.window()
    subprocess.run(['xcrun','simctl','addmedia',device,os.environ['LOGFORGE_DUO_FIXTURE']],check=True)
    driver.mobile('activateApp',bundleId='com.apple.mobileslideshow')
    driver.call('POST','/appium/settings',{'settings':{'defaultActiveApplication':'com.apple.mobileslideshow'}})
    for label in ('Continue', 'Not Now', 'Library'):
        buttons = driver.call('POST','/elements',{'using':'accessibility id','value':label})
        if buttons:
            driver.call('POST','/element/'+buttons[0][ELEMENT]+'/click',{})
    driver.capture('drop-source-photos')
    cells = until(lambda: driver.call('POST','/elements',{'using':'-ios predicate string',
        'value':"type == 'XCUIElementTypeCell' AND visible == true AND (label CONTAINS[c] 'video' OR label CONTAINS '1:00')"}))
    frame = driver.call('GET','/element/'+cells[0][ELEMENT]+'/rect')
    driver.mobile('dragFromToForDuration',duration=1.5,
        fromX=frame['x']+frame['width']/2,fromY=frame['y']+frame['height']/2,
        toX=destination['x']+destination['width']/2,toY=destination['y']+destination['height']/2)
    driver.call('POST','/appium/settings',{'settings':{'defaultActiveApplication':BUNDLE}})
    def admitted():
        state = driver.state()
        return state if not state['importing'] and len(state['ids']) == 1 else None
    state = until(admitted,timeout=120)
    assert state['statuses'] == ['queued'] and state['queueInstance'] == baseline['queueInstance']
    assert Path(state['sources'][0]).is_file(), 'Dropped file was not retained in the app container'
    driver.capture('drop-admitted-in-split-view')
    report['fileDrop'] = {'passed':True,'syntheticLayout':False,'source':'Photos','ids':state['ids']}
    report['checks'].append('actual ProRes video dragged from Photos into the split LogForge workspace')


def test_settings_and_appearance(driver, report):
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    original = driver.state()['exposure']
    driver.tap('conversionSettings')
    driver.tap('option-exposure')
    driver.tap('option-exposure-choice-1')
    display, _ = driver.pose(120, 'LANDSCAPE', 'inner', None)
    assert '1 EV' in driver.text('option-exposure'), 'Unsaved draft lost on fold'
    driver.capture('unsaved-settings-book', display)
    driver.tap('cancelSettings')
    assert until(driver.state)['exposure'] == original, 'Cancel committed a draft'
    driver.tap('conversionSettings')
    driver.tap('option-exposure')
    driver.tap('option-exposure-choice-1')
    driver.pose(180, 'PORTRAIT', 'inner', None)
    driver.tap('saveSettings')
    assert until(driver.state)['exposure'] == 1
    driver.tap('conversionSettings')
    driver.tap('option-exposure')
    driver.tap('option-exposure-choice-0')
    driver.tap('saveSettings')
    assert until(driver.state)['exposure'] == 0
    report['checks'].append('unsaved draft survives fold; cancel discards; save persists across rotation')
    for style in ('dark', 'light'):
        driver.mobile('setAppearance', style=style)
        until(lambda: driver.mobile('getAppearance')['style'] == style)
        driver.pose(180, 'PORTRAIT', 'inner', 'vertical')
        driver.capture('appearance-'+style)
    driver.mobile('setContentSize', size='accessibility-extra-extra-extra-large')
    until(lambda: driver.mobile('contentSize') == 'accessibility-extra-extra-extra-large')
    driver.pose(180, 'PORTRAIT', 'inner', 'single')
    driver.capture('accessibility-largest')
    driver.tap('conversionSettings')
    driver.capture('accessibility-settings')
    driver.tap('cancelSettings')
    driver.mobile('setContentSize', size='large')
    until(lambda: driver.mobile('contentSize') == 'large')
    # Change the actual system switch, then check SwiftUI receives the new setting.
    driver.call('POST', '/appium/settings', {'settings':{'defaultActiveApplication':'auto'}})
    driver.mobile('activateApp', bundleId='com.apple.Preferences')
    driver.scroll_to('Accessibility'); driver.tap('Accessibility')
    driver.scroll_to('Display & Text Size'); driver.tap('Display & Text Size')
    driver.scroll_to('Reduce Transparency'); driver.tap('Reduce Transparency')
    driver.mobile('activateApp', bundleId=BUNDLE)
    until(lambda: driver.state()['reduceTransparency'])
    driver.capture('reduce-transparency')
    driver.mobile('activateApp', bundleId='com.apple.Preferences')
    driver.tap('Reduce Transparency')
    driver.mobile('activateApp', bundleId=BUNDLE)
    until(lambda: not driver.state()['reduceTransparency'])
    report['checks'].append('system light/dark, largest accessibility text and Reduce Transparency')
    for language in ('en','zh-Hans','zh-Hant','fr','es'):
        driver.launch([], language)
        driver.pose(0, 'PORTRAIT', 'outer', 'single')
        driver.capture('empty-'+language)
        driver.tap('importVideo')
        driver.capture('import-'+language)
        driver.tap('cancelImport')
    driver.launch([])
    report['checks'].append('empty queue and import sheet in all five languages')


def test_conversion(driver, device, report):
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    container = Path(subprocess.check_output(['xcrun','simctl','get_app_container',device,BUNDLE,'data'], text=True).strip())
    movie = container / 'Documents/Duo-real-conversion.mov'
    shutil.copyfile(os.environ['LOGFORGE_DUO_FIXTURE'], movie)
    driver.launch(['--ui-import', movie.name])
    def imported():
        state = driver.state()
        return state if not state['importing'] and len(state['ids']) == 1 else None
    initial = until(imported, timeout=120)
    # Make an admitted file unavailable, exercising the real failure and retry path.
    source = Path(initial['sources'][0])
    backup = source.with_suffix('.temporarily-unavailable')
    source.rename(backup)
    try:
        driver.tap('conversionAction')
        until(lambda: not driver.state()['running'] and driver.state()['statuses'] == ['failed'])
        driver.scroll_to('retryFailed'); driver.tap('retryFailed')
        assert driver.state()['statuses'] == ['queued']
    finally:
        backup.rename(source)
    driver.tap('conversionAction')
    until(lambda: driver.state()['running'])
    driver.pose(120, 'LANDSCAPE', 'inner', 'horizontal', True)
    during = driver.state()
    assert during['running'], 'Conversion finished before fold; increase fixture duration'
    assert during['ids'] == initial['ids']
    driver.capture('real-conversion-book')
    driver.pose(0, 'PORTRAIT', 'outer', 'single')
    assert driver.state()['ids'] == initial['ids']
    driver.capture('real-conversion-outer')
    until(lambda: not driver.state()['running'], timeout=600)
    finished = driver.state()
    assert finished['statuses'] == ['complete'], finished
    assert len(finished['outputs']) == 1 and Path(finished['outputs'][0]).is_file()
    driver.pose(180, 'LANDSCAPE', 'inner', 'horizontal')
    share = 'shareVideo-' + initial['ids'][0]
    driver.scroll_to(share); driver.tap(share)
    until(lambda: driver.element('Save to Files'))
    driver.capture('completed-video-share')
    report['checks'].append('real ProRes import, unavailable-file failure, retry, running conversion across fold/display switch and share sheet')


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    driver = Driver()
    report = {'commit':os.environ.get('GITHUB_SHA'), 'syntheticLayout':False,
              'poses':[], 'checks':[], 'passed':False}
    try:
        until(lambda: driver.request('GET', '/status'), timeout=60)
        device = subprocess.check_output(['python3', 'tools/select-simulator.py', 'duo'], cwd=ROOT, text=True).strip()
        app = ROOT / 'build/DerivedData/Build/Products/Debug-iphonesimulator/LogForge For iPhone.app'
        assert app.is_dir()
        result = driver.request('POST', '/session', {'capabilities':{'alwaysMatch':{
            'platformName':'iOS', 'appium:automationName':'XCUITest', 'appium:udid':device,
            'appium:platformVersion':'27.1', 'appium:app':str(app),
            'appium:newCommandTimeout':180, 'appium:wdaLaunchTimeout':180000,
            'appium:connectHardwareKeyboard':False, 'appium:forceSimulatorSoftwareKeyboardPresence':True,
            'appium:processArguments':{'args':['-AppleLanguages','(en)','-AppleLocale','en_US',
                '--ui-testing','--ui-queue']}
        }, 'firstMatch':[{}]}}, timeout=300)
        driver.session = result['sessionId']
        baseline = until(driver.state)
        assert len(baseline['ids']) == 8
        poses = [('open-landscape',180,'LANDSCAPE','inner','horizontal',False),
                 ('open-portrait',180,'PORTRAIT','inner','vertical',False),
                 ('book',120,'LANDSCAPE','inner','horizontal',True),
                 ('tabletop',90,'PORTRAIT','inner','vertical',True),
                 ('tent-outer',90,'LANDSCAPE','outer','single',False),
                 ('closed-outer',0,'PORTRAIT','outer','single',False)]
        failures = []
        for name, angle, orientation, screen, layout, divided in poses:
            try:
                test_pose(driver, report, baseline, name, angle, orientation, screen, layout, divided)
            except (AssertionError, RuntimeError) as error:
                failures.append(name + ': ' + str(error))
                driver.capture(name+'-failed')
                report['poses'].append({'name':name, 'passed':False, 'failure':str(error)})
        # Independent scenarios restart only BETWEEN tests. A reset cannot serve
        # as evidence of continuity inside an individual scenario.
        for name, operation in [('splitView',test_split_view), ('keyboard',test_system_keyboard),
                                ('appearance',test_settings_and_appearance),
                                ('fileDrop',lambda d,r:test_file_drop(d,device,r)), ('multiWindow',test_multi_window),
                                ('conversion',lambda d,r:test_conversion(d,device,r))]:
            try:
                driver.call('POST','/appium/settings',{'settings':{'defaultActiveApplication':'auto'}})
                driver.mobile('setContentSize',size='large')
                # Remove restored scenes and sheet state between independent scenarios.
                # The Photos library survives; only this test app is reinstalled.
                driver.mobile('terminateApp',bundleId=BUNDLE)
                driver.mobile('removeApp',bundleId=BUNDLE)
                driver.mobile('installApp',app=str(app))
                driver.launch(['--ui-queue'])
                operation(driver, report)
                report.setdefault(name,{})['passed'] = True
            except (AssertionError, RuntimeError, StopIteration) as error:
                failures.append(name+': '+str(error))
                report.setdefault(name,{}).update(passed=False, failure=str(error))
                try: driver.capture(name+'-failed')
                except Exception: pass
        report['failures'] = failures
        assert not failures, '\n'.join(failures)
        report['passed'] = True
    except BaseException as error:
        report['failure'] = str(error)
        if driver.session:
            try: driver.capture('failure')
            except Exception: pass
        raise
    finally:
        (OUTPUT / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if driver.session:
            try: driver.call('DELETE','')
            except Exception: pass


def test_pose(driver, report, baseline, name, angle, orientation, screen, layout, divided):
    display, geometry = driver.pose(angle, orientation, screen, layout, divided)
    state = driver.state()
    assert state['ids'] == baseline['ids'], 'Queue identity changed during folding'
    assert state['statuses'] == baseline['statuses']
    shot = driver.capture(name, display)
    driver.tap('importVideo')
    until(lambda: driver.element('Choose Files'))
    driver.capture(name+'-import', display)
    driver.tap('cancelImport')
    driver.tap('conversionSettings')
    until(lambda: driver.text('settingsVersion') == '1.3.2 (261023A)')
    driver.capture(name+'-settings', display)
    driver.tap('cancelSettings')
    until(driver.state)
    report['poses'].append({'name':name, 'angle':angle, 'orientation':orientation,
        'screen':screen, 'display':display, 'geometry':geometry, 'passed':True, **shot})
    print('PASS pose:', name, flush=True)


if __name__ == '__main__':
    main()
