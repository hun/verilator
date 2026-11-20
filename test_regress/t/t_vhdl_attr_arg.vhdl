-- DESCRIPTION: Verilator: an attribute of a type mark with an argument folds in a value path
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
    o_pos  : out integer;
    o_size : out integer
  );
end entity t;

architecture rtl of t is
  -- The same expressions fold in a constant; they must fold in an assignment too
  constant folded_c : integer := natural'pos(3);
begin
  o_pos  <= natural'pos(3);
  o_size <= natural'pos(folded_c + 2);
end architecture rtl;
