-- DESCRIPTION: Verilator: a for loop with more iterations than the unroll limit is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The loop folds at most MAX_FOLD_ITERATIONS (4096) iterations when unrolled.
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is
  subtype big is integer range 0 to 5000;
begin
  process (a)
  begin
    for i in big loop
      null;
    end loop;
    y(0) <= a;
  end process;
end architecture rtl;
