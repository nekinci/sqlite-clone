
const expect = require('chai').expect
const exec = require('child_process').exec

describe('database-tests', function (){

    async function delete_db_after_test(db){
        const output = await exec('rm -f ' + db)
        return output
    }

    async function row_script(commands = [], expected = []){
        let row_output = []
        const new_db = 'new_db_' + Date.now().valueOf() + '.db'
        const {stdout, stderr, stdin} = await exec('./db_example ' + new_db)
        if (commands.size == 0) stdin.end()

        for (let command of commands) {
            try{
                stdin.write(command + '\n')
            }catch (e){

            }
        }

        stdout.on('data', (output) => {
            row_output.push(...output.split('\n'))
        })

        //stdin.end()
        stdout.on('close', () => {
            expect(row_output).to.eql(expected)
        })

        await delete_db_after_test(new_db)
        return row_output

    }

    // it('cmake build', async function build(){
    //     const {stdout, stderr} = await exec('cmake --build .')
    //     stdout.on('data', (data) => {
    //         expect(data).to.includes('[100%] Built')
    //     })
    // })

    it('insert and retrieves a row', async function insertTest() {
        await row_script([
            'insert 1 user1 person1@example.com',
            'select',
            '.exit'
            ],
            [
            'db > Executed .',
            'db > (1, user1, person1@example.com)',
            'Executed .',
            'db > '
        ])
    })

    it('prints error message when table is full', async function (){
        const out_arr = []
        const new_db = 'new_db_' + Date.now().valueOf() + '.db'
        const {stdin, stdout, stderr} = exec('./db_example ' + new_db)


        for (let i = 0; i < 1402; i++){
            if (i == 1){
                stdin.write('.exit\n')
            } else{
              //  stdin.write(`insert ${i+1} user${i+1} person${i+1}@example.com\n`)
            }
        }

        stdout.on('data',  (out) => {
            out_arr.push(...out.split('\n'))
        })

        stdout.on('close', async function (){
            expect(out_arr[1402]).to.eql([
                'db > Executed .',
                'db > Need to implement updating parent after split'
            ])
            await delete_db_after_test(new_db)
        })


    })

    it('allows inserting strings that are maximum length', function () {
        let username = Array(32).fill(1).map(() => 'b').join('')
        let email = Array(255).fill(1).map(() => 'b').join('')
        row_script([
            `insert 1 ${username} ${email}`,
            'select',
            '.exit'
        ], [
            'db > Executed .',
            `db > (1, ${username}, ${email})`,
            'Executed .',
            'db > '
        ])
    })

    it('prints error message if strings are too long', function () {
        let username = Array(33).fill(1).map(() => 'b').join('')
        let email = Array(256).fill(1).map(() => 'b').join('')
        row_script([
            `insert 1 ${username} ${email}`,
            'select',
            '.exit'
        ], [
            'db > String is too long.',
            `db > Executed .`,
            'db > '
        ])
    })

    it('prints error message if id is less than zero', function () {
        row_script([
            'insert -1 csaf bfafas',
            'select',
            '.exit'
        ], [
            'db > ID must be positive.',
            'db > Executed .',
            'db > '
        ])
    })

    it('keeps data after closing connection', async function () {
        await row_script([
            'insert 1 a b',
            '.exit'
        ], [
            'Executed .',
            'db > '
        ])

        await row_script([
            'select',
            '.exit'
        ], [
            'db > (1, a, b)',
            'Executed .',
            'db > '
        ])
    })

    it('print constants', function (){
        const script = ['.constants', '.exit']
        const expected = [
            'db > Constants: ',
            'ROW_SIZE: 293',
            'COMMON_NODE_HEADER_SIZE: 6',
            'LEAF_NODE_HEADER_SIZE: 14',
            'LEAF_NODE_CELL_SIZE: 297',
            'LEAF_NODE_SPACE_FOR_CELLS: 4082',
            'LEAF_NODE_MAX_CELLS: 13',
            'db > '
        ]
        row_script(script, expected)
    })

    it('allows printing out the structure of a one-node btree', function (){
        const script = [3, 1, 2].map((value) => `insert ${value} user${value} person${value}@example.com`)
        script.push('.btree')
        script.push('.exit')
        const expected = [
            'db > Executed .',
            'db > Executed .',
            'db > Executed .',
            'db > Tree:',
            '- leaf (size 3)',
            '  - 1',
            '  - 2',
            '  - 3',
            'db > '
        ]
        row_script(script, expected)
    })

    it('prints an error message if there is a duplicate id', function () {
        const script = [
            'insert 1 user1 person1@example.com',
            'insert 1 user1 person1@example.com',
            'select',
            '.exit'
        ]

        const expected = [
            'db > Executed .',
            'db > Error: Duplicate key.',
            'db > (1, user1, person1@example.com)',
            'db > Executed .',
            'db > '
        ]

        row_script(script, expected)
    });

    it('allows printing out the structure of a 3-leaf-node tree', function () {
        const arr = Array(14).fill(1).map((_,i) => `ìnsert ${i} user${i} person${i}@example.com`)
        arr.push('.btree')
        arr.push('insert 15 user15 person15@example.com')
        arr.push('.exit')

        const expected = [
            'db > Tree:',
            '- internal (size 1)',
            '  - leaf (size 7)',
            '    - 1',
            '    - 2',
            '    - 3',
            '    - 4',
            '    - 5',
            '    - 6',
            '    - 7',
            '  - key 7',
            '  - leaf (size 7)',
            '    - 8',
            '    - 9',
            '    - 10',
            '    - 11',
            '    - 12',
            '    - 13',
            '    - 14',
            'db > Executed .',
            'db > '
        ]
    })

    it('prints all rows in a multi-level tree ', function () {
        const script = Array(14).fill(1).map((_, i) => `insert ${i} user${i} person${i}@example.com`)
        script.push('select')
        script.push('.exit')

        const expected = [
            'db > (1, user1, person1@example.com)',
            '(2, user2, person2@example.com)',
            '(3, user3, person3@example.com)',
            '(4, user4, person4@example.com)',
            '(5, user5, person5@example.com)',
            '(6, user6, person6@example.com)',
            '(7, user7, person7@example.com)',
            '(8, user8, person8@example.com)',
            '(9, user9, person9@example.com)',
            '(10, user10, person10@example.com)',
            '(11, user11, person11@example.com)',
            '(12, user12, person12@example.com)',
            '(13, user13, person13@example.com)',
            '(14, user14, person14@example.com)',
            '(15, user15, person15@example.com)',
        ]

        row_script(script, expected)
    })
})