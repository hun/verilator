-- DESCRIPTION: Verilator: VHDL integer comparison combined with a bit comparison
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    req : in  std_logic;
    cnt : in  integer range 0 to 5;
    q   : out std_logic;
    q2  : out std_logic;
    qgt : out std_logic
  );
end t;

architecture behav of t is
begin
  -- Each condition mixes an integer test with a std_logic test through a logical
  -- operator; both comparison results must carry the same boolean type.
  q   <= '1' when req = '1' and cnt = 0 else '0';
  q2  <= '1' when cnt /= 3 or req = '0' else '0';
  qgt <= '1' when req = '1' and cnt > 1 else '0';
end behav;
