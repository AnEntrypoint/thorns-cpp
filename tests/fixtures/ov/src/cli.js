import { run } from './app.js';
import fs from 'fs';
export function main() { return run(fs.readdirSync('.').length); }
