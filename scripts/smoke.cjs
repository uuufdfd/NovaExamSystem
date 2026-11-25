// Optional Node.js 18+ harness. Launches only a new SQLite test server, never a real DB.
const fs=require('fs'),os=require('os'),p=require('path'),net=require('net'),cp=require('child_process'),assert=require('assert/strict');
const exe=process.env.SERVER_EXE;if(!exe)throw Error('Set SERVER_EXE to the freshly built QtExamServer binary');
const tmp=fs.mkdtempSync(p.join(os.tmpdir(),'nova-smoke-')),port=19090;
const server=cp.spawn(exe,['--host','127.0.0.1','--port',String(port),'--db-driver','sqlite','--db',p.join(tmp,'demo.db')],{windowsHide:true});
let logs='';server.stderr.on('data',b=>logs+=b);server.stdout.on('data',b=>logs+=b);
function request(data,success=true,split=false){return new Promise((resolve,reject)=>{const s=net.connect(port,'127.0.0.1');let buf='';s.setTimeout(5000,()=>s.destroy(Error('timeout')));s.on('error',reject);s.on('connect',()=>{const raw=JSON.stringify(data)+'\n';if(split){s.write(raw.slice(0,7));setTimeout(()=>s.write(raw.slice(7)),10);}else s.write(raw);});s.on('data',b=>{buf+=b;if(buf.includes('\n')){s.end();try{const r=JSON.parse(buf.split('\n')[0]);assert.equal(r.success,success,r.error);resolve(r);}catch(e){reject(e);}}});});}
async function main(){
 // Refuse to attach to an already-running service. The spawned process must remain alive.
 await new Promise(r=>setTimeout(r,800));if(server.exitCode!==null)throw Error('Test server exited: '+logs);
 await request({type:'ping'},true,true);
 await request({type:'list_users'},false);
 const login=async(u,pw,role)=>(await request({type:'login',username:u,password:pw,role})).token;
 const admin=await login('admin','admin123','admin'),teacher=await login('teacher001','123456','teacher');
 const call=(token,type,rest={},ok=true)=>request({token,type,...rest},ok);
 await request({type:'login',username:'admin',password:'wrong',role:'admin'},false);
 await call(admin,'create_user',{username:'demo_candidate',password:'demo123456',displayName:'演示考生',role:'student',note:'temporary smoke fixture'});
 const student=await login('demo_candidate','demo123456','student');
 await call(student,'list_users',{},false);
 const questions=(await call(teacher,'list_questions')).questions;assert(questions.length>=2);
 const examId=(await call(teacher,'create_exam',{exam:{name:'Synthetic smoke exam',course:'Demo',startAt:new Date(Date.now()-300000).toISOString(),endAt:new Date(Date.now()+3600000).toISOString(),durationMinutes:60,maxSubmissions:1},questionIds:questions.slice(0,2).map(q=>q.id)})).examId;
 assert(!(await call(student,'list_exams')).exams.some(e=>e.id===examId));
 const results=await Promise.all([0,1].map(()=>new Promise((resolve,reject)=>{request({token:teacher,type:'publish_exam',examId}).then(resolve).catch(async e=>{if(String(e).includes('已发布'))resolve({success:false});else reject(e);});})));
 assert.equal(results.filter(r=>r.success).length,1);
 assert((await call(student,'list_exams')).exams.some(e=>e.id===examId));
 const detail=await call(student,'get_exam_detail',{examId});
 const risk=await call(student,'record_abnormal_event',{examId,abnormalType:'SCREEN_SWITCH',description:'synthetic event'});assert.equal(risk.addedScore,6);assert.equal(risk.riskLevel,'低风险');
 const cooled=await call(student,'record_abnormal_event',{examId,abnormalType:'SCREEN_SWITCH',description:'cooldown check'});assert.equal(cooled.addedScore,0);
 const answers=detail.questions.map(q=>({questionId:q.id,answer:'Synthetic answer'}));
 await call(student,'save_draft',{examId,answers});await call(student,'submit_exam',{examId,answers});
 const sub=(await call(teacher,'list_gradable_submissions',{statusFilter:'pending'})).submissions.find(s=>s.examId===examId);assert(sub);
 await call(teacher,'lock_submission',{submissionId:sub.id});
 const d=await call(teacher,'get_submission_detail',{submissionId:sub.id});
 await call(teacher,'submit_grade',{submissionId:sub.id,answerGrades:d.answers.map(a=>({answerId:a.answerId,score:a.maxScore,comment:'demo'})),comment:'smoke test'});
 assert((await call(student,'list_my_submissions')).submissions.some(s=>s.id===sub.id&&s.status==='graded'));
 assert((await call(teacher,'list_risk_records')).records.some(r=>r.examId===examId&&r.riskScore===6));
 console.log('PASS: framing, login, denied access, exam visibility, duplicate publication, draft, submission, grading, risk score and cooldown.');
}
main().catch(e=>{console.error(e.message);process.exitCode=1;}).finally(()=>{server.kill();});
