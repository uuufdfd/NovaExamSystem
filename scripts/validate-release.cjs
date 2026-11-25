// Dependency-free packaging checks. Run from any working directory using Node.js 18+.
const fs=require('fs'),p=require('path');const root=p.resolve(__dirname,'..');
const files=d=>fs.readdirSync(d,{withFileTypes:true}).flatMap(e=>e.name==='.git'||(e.isDirectory()&&e.name.startsWith('build'))?[]:e.isDirectory()?files(p.join(d,e.name)):[p.join(d,e.name)]);
const errors=[];let count=0;
for(const f of files(root)){
 const rel=p.relative(root,f);count++;
 if(/\.(exe|dll|db|onnx|docx|pdf|pyc|log)$/.test(f))errors.push(rel+': disallowed release artifact');
 if(!/\.(md|cpp|h|py|cjs|sql|ps1|sh|ini|env|qrc|rc|service|txt)$/.test(f))continue;
 const s=fs.readFileSync(f,'utf8');
 if(/-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\bgh[pousr]_[A-Za-z0-9]{20,}|\bgithub_pat_[A-Za-z0-9_]{20,}/.test(s))errors.push(rel+': possible credential');
 if(f.endsWith('.md'))for(const m of s.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g)){const target=m[1].split('#')[0];if(target&&!/^(https?:|mailto:)/.test(target)&&!fs.existsSync(p.resolve(p.dirname(f),target)))errors.push(rel+': missing '+target);}
}
if(errors.length){console.error(errors.join('\n'));process.exitCode=1;}else console.log('PASS: '+count+' release files; artifact, token-pattern and local Markdown-link checks.');
