-- DESCRIPTION: Verilator: An exit label has to name an enclosing loop
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (a : in bit; y : out bit);
end;

architecture rtl of t is
begin
  process (a)
  begin
    other : for i in 0 to 1 loop
      y <= a;
    end loop;
    inner : for i in 0 to 1 loop
      exit other when a = '1';
    end loop;
  end process;
end;
