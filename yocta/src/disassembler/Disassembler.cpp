#include "Disassembler.h"
#include "OperationCodes.h"

void yo::Disassembler::disassemble(const Chunk& array, const char* instructionSetName)
{
	printf("-=-= Disassembly : %s =-=-\n", instructionSetName);

	for (unsigned int offset = 0; offset < array.data.size();)
		offset = disassembleInstruction(array, offset);
}

unsigned int yo::Disassembler::disassembleInstruction(const Chunk& chunk, int offset)
{
	printf("%04d\t", offset);

	uint8_t instruction = chunk.data[offset];

	printf("%04d\t", chunk.lines[offset]);

	switch (instruction)
	{
	case (uint8_t)OPCode::None:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_RETURN:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_CONSTANT:
		return constantInstruction(instruction, chunk, offset);

	case (uint8_t)OPCode::OP_NEGATE:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_ADD:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_SUB:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_MULT:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_DIV:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_NONE:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_TRUE:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_FALSE:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_NOT:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_EQUAL:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_LESS:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_GREATER:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_PRINT:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_POP_BACK:
		return simpleInstruction(instruction, offset);

	case (uint8_t)OPCode::OP_DEFINE_GLOBAL_VAR:
		return constantInstruction(instruction, chunk, offset);

	case (uint8_t)OPCode::OP_GET_GLOBAL_VAR:
		return constantInstruction(instruction, chunk, offset);

	case (uint8_t)OPCode::OP_SET_GLOBAL_VAR:
		return constantInstruction(instruction, chunk, offset);

	case (uint8_t)OPCode::OP_SET_LOCAL_VAR:
		return byteInstruction(instruction, chunk, offset);

	case (uint8_t)OPCode::OP_GET_LOCAL_VAR:
		return byteInstruction(instruction, chunk, offset);

	case (uint8_t)OPCode::OP_JUMP:
		return jumpInstruction(instruction, 1, chunk, offset);

	case (uint8_t)OPCode::OP_JUMP_IF_FALSE:
		return jumpInstruction(instruction, 1, chunk, offset);

	case (uint8_t)OPCode::OP_LOOP:
		return jumpInstruction(instruction, -1, chunk, offset);

	default:
		printf("Unknown opcode [%s]\n", translateCode((OPCode)instruction));
		return offset + 1;
	}
}

unsigned int yo::Disassembler::simpleInstruction(uint8_t code, int offset)
{
	printf("%s\n", translateCode((OPCode)code));
	return offset + 1;
}

unsigned int yo::Disassembler::constantInstruction(uint8_t code, const Chunk& chunk, int offset)
{
	uint8_t constant = chunk.data[++offset];

	Value value = chunk.constantPool[constant];
	if (value.variantValue.index() == 2)
	{
		StringObject* object = getStringObject(value);
		printf("%s\t[Index]: %d | [Value]: %s\n", translateCode((OPCode)code), constant, object->data.c_str());
	}
	else if (value.variantValue.index() == 1)
	{
		double v = std::get<double>(value.variantValue);
		printf("%s\t[Index]: %d | [Value]: %f\n", translateCode((OPCode)code), constant, v);
	}
	else if (value.variantValue.index() == 0)
	{
		bool v = std::get<bool>(value.variantValue);
		printf("%s\t[Index]: %d | [Value]: %s\n", translateCode((OPCode)code), constant, v ? "true" : "false");
	}

	return offset + 1;
}

unsigned int yo::Disassembler::byteInstruction(uint8_t code, const Chunk& chunk, int offset)
{
	uint8_t slot = chunk.data[offset + 1];
	printf("%-16s %4d\n", translateCode((OPCode)code), slot);
	return offset + 2;
}

unsigned int yo::Disassembler::jumpInstruction(uint8_t code, int sign, const Chunk& chunk, int offset)
{
	uint16_t jump = (uint16_t)(chunk.data[offset + 1] << 8);
	jump |= chunk.data[offset + 2];
	printf("%-16s %4d -> %d\n", translateCode(OPCode(code)), offset, offset + 3 + sign * jump);
	return offset + 3;
}
