-- DESCRIPTION: Verilator: VHDL character-type attributes
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk  : in  std_logic;
    code : in  integer;
    c97  : out integer;
    cupp : out integer;
    cnxt : out integer;
    cpvs : out integer;
    clow : out integer;
    chgh : out integer;
    clen : out integer
  );
end entity;

architecture rtl of t is
  signal ca : character;
begin
  -- 'val builds a character from its position; 'pos reads the code back
  ca <= character'val(code);
  c97 <= character'pos(ca);
  -- 'pos on a character literal is a static code; 'a - 'A == 32
  cupp <= character'pos('a') - character'pos('A');
  -- 'succ/'pred step the code by one
  cnxt <= character'pos(character'succ(ca));
  cpvs <= character'pos(character'pred(ca));
  -- static range attributes of the character type
  clow <= character'pos(character'low);
  chgh <= character'high;
  clen <= character'length;
end architecture;
