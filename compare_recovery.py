from pathlib import Path
import re
pairs=[('git_recovery/05aded3d47fe133dd99c1292c213e7fa864c16e5.txt','engine/include/Application.h'),('git_recovery/0f189019aceccd8e443b49bd2da4c63bfa14ca9c.txt','engine/CMakeLists.txt')]
for a,b in pairs:
    raw_a=Path(a).read_bytes()
    raw_b=Path(b).read_bytes()
    def norm(data):
        if data.startswith(b'\xff\xfe') or data.startswith(b'\xfe\xff'):
            txt=data.decode('utf-16',errors='replace')
        else:
            txt=data.decode('utf-8',errors='replace')
        return re.sub(r'\s+',' ',txt).strip()
    na=norm(raw_a)
    nb=norm(raw_b)
    print(a,b,'equal?',na==nb)
    if na!=nb:
        print('A snippet:',na[:120])
        print('B snippet:',nb[:120])
