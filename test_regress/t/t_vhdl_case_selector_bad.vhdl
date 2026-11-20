-- DESCRIPTION: Verilator: VHDL case and selected selectors must be discrete
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- IEEE 1076-2008 10.9: a selector is discrete or an array of a discrete
-- element type. A record and an integer array are neither.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  type rec_t is record
    a : integer;
  end record;
  signal r  : rec_t;
  signal iv : integer_vector(1 downto 0);
begin
  process (r, iv)
  begin
    case r is
      when others => y <= 0;
    end case;
    case iv is
      when others => y <= 1;
    end case;
  end process;

  with r select y <= 2 when others;
  with iv select y <= 3 when others;
end;
