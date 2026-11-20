export function load(db) { return db.query('SELECT * FROM users WHERE id = ?'); }
export const note = 'select a color';
