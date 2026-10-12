'use strict';
// Offline IPC bridge: execute the actual ST source against ARM-model feedback.
// This is not a TwinCAT runtime. The underlying tests/parser state that clearly.
const readline = require('readline');
const originalLog = console.log;
console.log = () => {};
const st = require('../examples/TwinCAT_CSP_SmoothRepeat/verify_example.js');
console.log = originalLog;
const controller = st.make();
readline.createInterface({input:process.stdin}).on('line',line=>{
    try {
        st.set(controller,JSON.parse(line));
        st.cycle(controller);
        const keys=['qControlWord','qTargetPosition','qTargetVelocity','qMode',
            'bReady','bRun','bStopLatched','nStopReason','nPhase','nCompletedTurns'];
        const result={};
        for(const key of keys)result[key]=st.get(controller,key);
        process.stdout.write(JSON.stringify(result)+'\n');
    } catch(error) {
        process.stderr.write(String(error.stack)+'\n');
        process.exit(1);
    }
});
