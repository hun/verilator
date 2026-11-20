-- DESCRIPTION: Verilator: a predefined scalar type mark as a conversion
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o_bool  : out std_logic;
    o_logic : out std_logic
  );
end entity t;

architecture rtl of t is
  constant one : std_ulogic := '1';
  -- A predefined scalar type mark applied to a value is a conversion, not a name
  constant flag : boolean    := boolean(3 = 3);
  constant bit1 : std_logic  := std_logic(one);
begin
  o_bool  <= '1' when flag else '0';
  o_logic <= bit1;
end architecture rtl;